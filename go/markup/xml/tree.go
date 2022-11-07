package xml

import (
	"fmt"
	"io"

	"github.com/dedis/matchertext/go/internal/util"
	"github.com/dedis/matchertext/go/markup/ast"
)

type TreeWriter struct {
	w util.AtomWriter
}

// NewTreeWriter creates and returns a TreeWriter that writes output to w.
func NewTreeWriter(w io.Writer) *TreeWriter {
	return &TreeWriter{w: util.ToAtomWriter(w)}
}

// WriteAST writes a slice of markup AST nodes to the encoder's output.
func (e *TreeWriter) WriteAST(ns []ast.Node) (err error) {

	for i := range ns {
		switch n := ns[i].(type) {

		case ast.RawText: // Plain text sequence, raw or cooked
			e.text(n.Text(), n.IsRaw(), EscBasic)

		case ast.Text: // Plain (cooked) text sequence
			err = e.text(n.Text(), false, EscBasic)

		case ast.Reference:
			err = e.reference(n.Reference())

		case ast.Element:
			err = e.element(n.Element())

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
	err = util.Flush(e.w)
	return
}

func (e *TreeWriter) text(s string, raw bool, esc Escaper) error {
	if raw {
		return e.rawText(s)
	}
	return esc.WriteStringTo(e.w, s)
}

const rsRaw = "]]]]><![CDATA[>"

// Write raw text as a CDATA section
func (e *TreeWriter) rawText(s string) error {
	if s == "" {
		return nil
	}

	// Start a CDATA section
	if _, err := e.w.WriteString("<![CDATA["); err != nil {
		return err
	}

	// Write the section content, replacing ]]> terminator sequences
	l := 0
	for i := 0; i <= len(s)-3; {
		if s[i] == ']' && s[i+1] == ']' && s[i+2] == '>' {

			// Write unescaped text up to escaped character
			if _, err := e.w.WriteString(s[l:i]); err != nil {
				return err
			}

			// Write the disgusting replacement sequence
			if _, err := e.w.WriteString(rsRaw); err != nil {
				return err
			}

			i += 3
			l = i
		} else {
			i++
		}
	}
	_, err := e.w.WriteString(s[l:])

	// End the CDATA section
	if _, err := e.w.WriteString("]]>"); err != nil {
		return err
	}

	return err
}

// Write a reference to XML output
func (e *TreeWriter) reference(name string) error {

	// XXX check that it's actually a valid XML reference string?

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

func (e *TreeWriter) element(name string, attr []ast.Attribute,
	content []ast.Node) (err error) {

	// XXX check that it's actually a valid XML name string?

	// write the left-angle bracket and element name
	if err := e.w.WriteByte('<'); err != nil {
		return err
	}
	if _, err := e.w.WriteString(name); err != nil {
		return err
	}

	// write the element attributes
	for _, a := range attr {
		if err := e.w.WriteByte(' '); err != nil {
			return err
		}
		name, val := a.Attribute()
		if _, err := e.w.WriteString(name); err != nil {
			return err
		}
		if err := e.w.WriteByte('='); err != nil {
			return err
		}
		if err := e.w.WriteByte('"'); err != nil {
			return err
		}
		for _, n := range val {
			switch n := n.(type) {
			case ast.RawText:
				err = e.text(n.Text(), n.IsRaw(), EscInQuot)

			case ast.Text:
				err = e.text(n.Text(), false, EscInQuot)

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

	// make the start tag self-closing if it has no content
	if len(content) == 0 {
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
