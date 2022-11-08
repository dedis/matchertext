package minml

import (
	"fmt"
	"io"

	"github.com/dedis/matchertext/go/internal/util"
	"github.com/dedis/matchertext/go/markup/ast"
)

// TreeWriter writes a markup AST to an output stream in MinML syntax.
//
// The literal text content in the AST must already be valid matchertext:
// that is, all literal matchers must match within the same markup sequence.
// MatcherTransformer may be used to escape unmatched matchers if needed.
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

	// Write the markup content
	if err := e.nodes(ns); err != nil {
		return err
	}

	// Flush the output stream in case it's buffered
	return util.Flush(e.bw)
}

func (e *TreeWriter) nodes(ns []ast.Node) (err error) {
	for i := range ns {
		switch n := ns[i].(type) {
		case ast.RawText:
			err = e.text(n.Text(), n.IsRaw(), escMarkup)

		case ast.Text: // Plain text sequence, raw or cooked
			err = e.text(n.Text(), false, escMarkup)

		case ast.Reference:
			err = e.reference(n.Reference())

		case ast.Element:
			err = e.element(n)

		case ast.Comment:
			err = e.comment(n.Comment())

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

	// Normal text: just "escape" false elements or character references
	escelt := (esc & escElement) != 0
	escref := (esc & escReference) != 0
	for i := 0; i < len(text); i++ {
		b := text[i]
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

// Write a reference to XML output
func (e *TreeWriter) reference(name string) error {

	// XXX verify that name is a valid MinML reference name?

	return e.strings("[", name, "]")
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
			for _, n := range val {
				switch n := n.(type) {
				case ast.Text:
					err = e.text(n.Text(), false,
						escReference)

				case ast.Reference:
					err = e.reference(n.Reference())

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

type encError string

func (e encError) Error() string {
	return string(e)
}
