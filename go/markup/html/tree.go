package html

import (
	"fmt"
	"io"

	"github.com/dedis/matchertext/go/internal/util"
	"github.com/dedis/matchertext/go/markup/ast"
	"github.com/dedis/matchertext/go/markup/xml"
)

type TreeWriter struct {
	w util.AtomWriter
}

// NewTreeWriter creates and returns a new encoder that writes output to w.
func NewTreeWriter(w io.Writer) *TreeWriter {
	return &TreeWriter{w: util.ToAtomWriter(w)}
}

// WriteAST writes a slice of markup AST nodes to the encoder's output.
func (e *TreeWriter) WriteAST(ns []ast.Node) (err error) {

	for i := range ns {
		switch n := ns[i].(type) {

		case ast.Text: // Plain text sequence, raw or cooked
			err = e.text(n.Text(), xml.EscBasic)

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

	// Flush the output stream in case it's buffered
	return util.Flush(e.w)
}

func (e *TreeWriter) text(s string, esc xml.Escaper) error {
	return esc.WriteStringTo(e.w, s)
}

// Write a reference to XML output
func (e *TreeWriter) reference(name string) error {

	if err := e.w.WriteByte('&'); err != nil {
		return err
	}
	if _, err := e.w.WriteString(name); err != nil {
		return err
	}
	if err := e.w.WriteByte(';'); err != nil {
		return err
	}
	return nil
}

// isVoid represents the set of void elements in HTML.
// https://html.spec.whatwg.org/multipage/syntax.html#void-elements
var isVoid = map[string]bool{
	"area":   true,
	"base":   true,
	"br":     true,
	"col":    true,
	"embed":  true,
	"hr":     true,
	"img":    true,
	"input":  true,
	"link":   true,
	"meta":   true,
	"source": true,
	"track":  true,
	"wbr":    true,
}

func (e *TreeWriter) element(elt ast.Element) (err error) {
	name, attrs, content := elt.Element()

	// write the left-angle bracket and element name
	if err := e.w.WriteByte('<'); err != nil {
		return err
	}
	if _, err := e.w.WriteString(name); err != nil {
		return err
	}

	// write the element attributes
	for _, a := range attrs {
		name, value := a.Attribute()
		if err := e.w.WriteByte(' '); err != nil {
			return err
		}
		if _, err := e.w.WriteString(name); err != nil {
			return err
		}
		if err := e.w.WriteByte('='); err != nil {
			return err
		}
		if err := e.w.WriteByte('"'); err != nil {
			return err
		}
		for _, n := range value {
			switch n := n.(type) {
			case ast.Text:
				err = e.text(n.Text(), xml.EscInQuot)

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
		if err := e.w.WriteByte('"'); err != nil {
			return err
		}
	}

	// make the start tag self-closing if appropriate -
	// HTML allows this for the specific list of void elements
	if len(content) == 0 && isVoid[name] {
		_, err := e.w.WriteString("/>")
		return err
	}

	// complete the start tag
	if err := e.w.WriteByte('>'); err != nil {
		return err
	}

	// recursively write the element content
	if err := e.WriteAST(content); err != nil {
		return err
	}

	// write the end tag
	if _, err := e.w.WriteString("</"); err != nil {
		return err
	}
	if _, err := e.w.WriteString(name); err != nil {
		return err
	}
	if err := e.w.WriteByte('>'); err != nil {
		return err
	}

	return nil
}

func (e *TreeWriter) comment(s string) error {

	// open the comment
	if _, err := e.w.WriteString("<!--"); err != nil {
		return err
	}

	// write the content text, watching for illegal -- sequences
	l := 0
	for i := 0; i <= len(s)-2; {
		if s[i] == '-' && s[i+1] == '-' {

			// Write unescaped text up through the first dash
			if _, err := e.w.WriteString(s[l : i+1]); err != nil {
				return err
			}

			// Escape the disallowed second dash
			if _, err := e.w.WriteString("&#45;"); err != nil {
				return err
			}

			i += 2
			l = i
		} else {
			i++
		}
	}
	if _, err := e.w.WriteString(s[l:]); err != nil {
		return err
	}

	// close the comment
	if _, err := e.w.WriteString("-->"); err != nil {
		return err
	}

	return nil
}

type encError string

func (e encError) Error() string {
	return string(e)
}
