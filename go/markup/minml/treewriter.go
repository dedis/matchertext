package minml

import (
	"fmt"
	"io"

	"github.com/dedis/matchertext/go/internal/util"
	"github.com/dedis/matchertext/go/markup/ast"
	"github.com/dedis/matchertext/go/matchertext"
)

// TreeWriter writes a markup AST to an output stream in MinML syntax.
//
// Literal matchers that do not match within the same markup sequence
// are written as matcher escapes such as \o() and \c[].
type TreeWriter struct {
	bw util.AtomWriter // output stream to write to

	last byte // last byte written
	pref bool // possible character reference
}

// NewTreeWriter creates and returns a TreeWriter that writes output to w.
func NewTreeWriter(w io.Writer) *TreeWriter {
	return &TreeWriter{bw: util.ToAtomWriter(w)}
}

// WriteAST writes a slice of markup AST nodes to the encoder's output.
func (e *TreeWriter) WriteAST(ns []ast.Node) (err error) {

	// Pretend the entire markup is surrounded by a bracket pair.
	e.last, e.pref = '[', false

	// Write the document type, which only the first node may declare
	if kind := ast.DoctypeOf(ns); kind != "" {
		if err := checkDoctype(kind); err != nil {
			return err
		}
		if err := e.strings("![", kind, "]"); err != nil {
			return err
		}
		ns = ns[1:]
	}

	// Write the markup content
	if err := e.nodes(ns); err != nil {
		return err
	}

	// Flush the output stream in case it's buffered
	return util.Flush(e.bw)
}

func (e *TreeWriter) nodes(ns []ast.Node) (err error) {
	if ns, err = MatcherTransformer.Transform(ns); err != nil {
		return err
	}
	for i := 0; i < len(ns); i++ {
		switch n := ns[i].(type) {
		case ast.RawText:
			err = e.text(n.Text(), n.IsRaw(), escMarkup)

		case ast.Text: // Plain text sequence, raw or cooked
			s, k := textRun(ns[i:])
			i += k - 1
			err = e.text(s, false, escMarkup)

		case ast.Reference:
			err = e.reference(n.Reference(), escMarkup)

		case ast.Element:
			err = e.element(n)

		case ast.Comment:
			err = e.comment(n.Comment())

		case ast.Doctype:
			err = encError("document type is not the first node")

		default:
			err = encError(fmt.Sprintf("unknown node %v", n))
		}
		if err != nil {
			return
		}
	}
	return nil
}

func (e *TreeWriter) text(text string, raw bool, esc escaper) error {

	// Handle raw matchertext sections
	if raw {
		return e.open("+", "[", text, "]")
	}

	// Normal text: just "escape" false elements, character references, or matcher escapes
	escelt := (esc & escElement) != 0
	escref := (esc & escReference) != 0
	for i := 0; i < len(text); i++ {
		b := text[i]
		if b == '\\' && i+2 < len(text) && (text[i+1] == 'o' || text[i+1] == 'c') &&
			matchertext.IsOpener(text[i+2]) {

			// write the backslash as a reference so that the pair is no escape
			if err := e.reference("#92", esc); err != nil {
				return err
			}
			continue
		}
		if ((b == '[' || b == '{') && escelt && isNameByte(e.last)) ||
			(b == ']' && escref && e.pref && isNameByte(e.last)) {

			// separate the bracket from the prior text
			if err := e.strings(" <"); err != nil {
				return err
			}
		}
		if err := e.writeByte(b); err != nil {
			return err
		}
	}
	return nil
}

func (e *TreeWriter) open(eln string, ss ...string) error {

	// Separate element name from prior text if needed
	pad := ""
	if eln != "" && isNameByte(e.last) {
		pad = " <" // separate with a space sucker
	}

	// Write the padding, element name, and additional strings
	if err := e.strings(pad, eln); err != nil {
		return err
	}
	return e.strings(ss...)
}

func (e *TreeWriter) strings(ss ...string) error {
	for _, s := range ss {
		for i := 0; i < len(s); i++ {
			if err := e.writeByte(s[i]); err != nil {
				return err
			}
		}
	}
	return nil
}

// Maintain some running state about the MinML text we have written:
// the last byte seen, and whether we're looking at a possible reference,
// i.e., an open bracket followed by a continuous run of name bytes.
func (e *TreeWriter) writeByte(b byte) error {
	e.last = b
	e.pref = (b == '[') || (e.pref && isNameByte(b))
	return e.bw.WriteByte(b)
}

// Write a character reference or a matcher escape
func (e *TreeWriter) reference(name string, esc escaper) error {
	if IsEscape(name) {
		return e.strings(name)
	}

	// XXX verify that name is a valid MinML reference name?

	// separate the bracket from prior text that would be an element name
	pad := ""
	if esc&escElement != 0 && isNameByte(e.last) {
		pad = " <"
	}
	return e.strings(pad, "[", name, "]")
}

// textRun joins the text of the plain Text nodes at the start of ns
// and returns the number of those nodes,
// so that text sees each \o or \c together with the byte after it.
func textRun(ns []ast.Node) (string, int) {
	s := ns[0].(ast.Text).Text()
	k := 1
	for ; k < len(ns); k++ {
		if _, raw := ns[k].(ast.RawText); raw {
			break
		}
		t, ok := ns[k].(ast.Text)
		if !ok {
			break
		}
		s += t.Text()
	}
	return s, k
}

func (e *TreeWriter) element(elt ast.Element) (err error) {
	name, attrs, content := elt.Element()

	// First write open padding if needed and the element name
	if err := e.open(name); err != nil {
		return err
	}

	// Write the element attributes if any
	if len(attrs) > 0 {
		if err := e.writeByte('{'); err != nil {
			return err
		}
		for i, a := range attrs {

			// write the attribute name and value opener
			name, val := a.Attribute()
			if err := e.strings(name, "=["); err != nil {
				return err
			}

			// write the attribute value
			if val, err = MatcherTransformer.Transform(val); err != nil {
				return err
			}
			for j := 0; j < len(val); j++ {
				switch n := val[j].(type) {
				case ast.Text:
					s, k := textRun(val[j:])
					j += k - 1
					err = e.text(s, false, escValue)

				case ast.Reference:
					err = e.reference(n.Reference(), escValue)

				default:
					err = encError(fmt.Sprintf(
						"unknown value node %v", n))
				}
				if err != nil {
					return err
				}
			}

			// write the close bracket and potential space
			end := "]"
			if i+1 < len(attrs) {
				end = "] "
			}
			if err := e.strings(end); err != nil {
				return err
			}
		}
		if err := e.writeByte('}'); err != nil {
			return err
		}
	}

	// Finally write the element content
	if err := e.writeByte('['); err != nil {
		return err
	}
	if err := e.nodes(content); err != nil {
		return err
	}
	if err := e.writeByte(']'); err != nil {
		return err
	}
	return nil
}

func (e *TreeWriter) comment(s string) error {

	return e.open("-", "[", s, "]")
}

// checkDoctype returns an error unless kind is a MinML document type.
func checkDoctype(kind string) error {
	if kind != "html" && kind != "xml" {
		return encError(fmt.Sprintf("document type %q is neither html nor xml", kind))
	}
	return nil
}

type encError string

func (e encError) Error() string {
	return string(e)
}
