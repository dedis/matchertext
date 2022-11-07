package ast

import (
	"io"

	"github.com/dedis/matchertext/go/matchertext"
)

// A Transformer takes a slice of AST nodes from src,
// performs some transformation on any or all of them,
// and returns the resulting transformed slice of nodes.
//
// A particular Transformer typically recognizes and transforms
// only certain selected nodes, leaving others unmodified.
// A transformer may modify the existing slice in place,
// may insert and remove nodes,
// and may return either the existing slice or a new slice.
//
// A Transformer must transform Attribute nodes only into Attribute nodes.
type Transformer interface {
	Transform(ns []Node) ([]Node, error)
}

// A MatcherTransformer converts arbitrary markup
// into matchertext-compliant markup.
// This means that all matcher characters
// (parentheses, square brackets, and curly braces)
// appearing within literal text match properly
// at the same structural level within the AST.
type MatcherTransformer struct {

	// If non-nil, Escaper must map one of the six ASCII matchers
	// into a corresponding named or numeric character reference.
	// If nil, defaults to producing decimal numeric character references
	// for maximum compatibility (at the cost of ugliness).
	Escaper func(b byte) string
}

func (mt *MatcherTransformer) Transform(ns []Node) ([]Node, error) {

	// Determine the escaper function we should use on unmatched matchers
	esc := mt.Escaper
	if esc == nil {
		esc = defaultEscaper
	}

	// Identify the unmatched literal matchers, if any, we need to escape
	os, err := matchertext.UnmatchedOffsets(newTextReader(ns))
	if err != nil {
		return nil, err
	}
	if len(os) == 0 {
		return ns, nil // already matchertext - nothing to do
	}
	os.Sort() // we need the offsets sorted for in-order use

	ofs := int64(0)
	nns := []Node{}
	for len(ns) > 0 {
		n := ns[0]  // grab the next source node
		ns = ns[1:] // consume it

		if tn, ok := n.(Text); ok { // handle a Text node
			i := 0
			s := tn.Text()
			l := len(s)

			// escape any unmatched matchers in this Text node
			for len(os) > 0 && (os[0]-ofs) < int64(l) {
				o := int(os[0] - ofs)

				// Copy text before the next unmatched matcher
				if o > i {
					nt := NewText(s[i:o])
					nns = append(nns, nt)
				}

				// Escape the unmatched matcher
				nr := NewReference(esc(s[o]))
				nns = append(nns, nr)

				os = os[1:] // consume this offset
				i = o + 1   // and this unmatched matcher
			}

			// Copy text after the last unmatched matcher in it
			if i < l {
				nt := NewText(s[i:l])
				nns = append(nns, nt)
			}

			ofs += int64(l) // adjust offset for consumed text

		} else {
			nns = append(nns, n) // no change to non-text nodes
		}
	}
	return nns, nil
}

func defaultEscaper(b byte) string {
	switch b {
	case '(':
		return "#40"
	case ')':
		return "#41"
	case '[':
		return "#91"
	case ']':
		return "#93"
	case '{':
		return "#123"
	case '}':
		return "#125"
	default:
		panic("Escaper argument must be a matcher")
	}
}

// textReader reads bytes from only Text markup nodes, ignoring all others.
// Implements io.Reader and io.ByteScanner interfaces.
type textReader struct {
	s  string // string we're currently reading
	i  int    // current position in that string
	ns []Node // remaining AST node(s) to read
}

func newTextReader(ns []Node) *textReader {
	return &textReader{ns: ns}
}

func (tr *textReader) ReadByte() (byte, error) {

	// Return the next byte in the current string if there is one
	if tr.i < len(tr.s) {
		b := tr.s[tr.i]
		tr.i++
		return b, nil
	}

	// Otherwise, find the next Text node
	for len(tr.ns) > 0 {
		n := tr.ns[0]
		tr.ns = tr.ns[1:]
		if tn, ok := n.(Text); ok {
			tr.s = tn.Text()
			if len(tr.s) > 0 {
				tr.i = 1            // consume first byte
				return tr.s[0], nil // return it
			}
		}
	}
	return 0, io.EOF
}

func (tr *textReader) UnreadByte() error {
	tr.i--
	return nil
}

func (tr *textReader) Read(p []byte) (n int, err error) {
	for i := range p {
		b, err := tr.ReadByte()
		if err != nil && i > 0 {
			return i, nil
		} else if err != nil {
			return 0, err
		}
		p[i] = b
	}
	return len(p), nil
}
