package xml

import (
	"fmt"
	"io"

	"github.com/dedis/matchertext/go/markup/ast"
	"github.com/dedis/matchertext/go/internal/util"
)

type Encoder struct {
	w util.AtomWriter
}

// NewEncoder creates and returns a new encoder that writes output to w.
func NewEncoder(w io.Writer) *Encoder {
	return &Encoder{w: util.ToAtomWriter(w)}
}

// Encode writes a slice of markup AST nodes to the encoder's output.
func (e *Encoder) Encode(ns []ast.Node) (err error) {

	for i := range ns {
		switch n := ns[i].(type) {

		case ast.Text: // Plain text sequence, raw or cooked
			if n.Raw {
				err = e.rawText(n.Text)
			} else {
				err = e.text(n.Text, EscBasic)
			}

		case ast.Reference:
			err = e.reference(n.Name)

		case ast.Element:
			err = e.element(n.Name, n.Attribs, n.Content)

		case ast.Comment:
			err = e.comment(n.Text)

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

func (e *Encoder) text(s string, esc Escaper) error {
	return esc.WriteStringTo(e.w, s)
}

const rsRaw = "]]]]><![CDATA[>"

// Write raw text as a CDATA section
func (e *Encoder) rawText(s string) error {
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
func (e *Encoder) reference(name string) error {

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

func (e *Encoder) element(name string, attr []ast.Attribute,
	content []ast.Node) (err error) {

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
		if _, err := e.w.WriteString(a.Name); err != nil {
			return err
		}
		if err := e.w.WriteByte('='); err != nil {
			return err
		}
		if err := e.w.WriteByte('"'); err != nil {
			return err
		}
		for _, n := range a.Value {
			switch n := n.(type) {
			case ast.Text:
				err = e.text(n.Text, EscInQuot)

			case ast.Reference:
				err = e.reference(n.Name)

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
	if err := e.Encode(content); err != nil {
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

func (e *Encoder) comment(s string) error {

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
