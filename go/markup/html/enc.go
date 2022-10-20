package html

import (
	"bufio"
	"fmt"
	"io"

	"github.com/dedis/matchertext/go/markup/ast"
)

// This interface defines the writing utility classes we need.
// We assume the interface passed to NewEncoder is efficient enough
// if it supports this interface, otherwise we interpose a bufio.
type writer interface {
	io.ByteWriter
	io.StringWriter
}

type flusher interface {
	Flush() error
}

type Encoder struct {
	w writer
}

// NewEncoder creates and returns a new encoder that writes output to w.
func NewEncoder(w io.Writer) *Encoder {
	e := &Encoder{}
	return e.setWriter(w)
}

// SetWriter initializes encoder e to write output to w, and returns e.
func (e *Encoder) setWriter(w io.Writer) *Encoder {
	if bw, ok := w.(writer); ok {
		e.w = bw
	} else {
		e.w = bufio.NewWriter(w)
	}
	return e
}

// Encode writes a slice of markup AST nodes to the encoder's output.
func (e *Encoder) Encode(ns []ast.Node) (err error) {

	for i := range ns {
		switch n := ns[i].(type) {

		case ast.Text: // Plain text sequence, raw or cooked
			err = e.text(n.Text, escBasic)

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
	if f, ok := e.w.(flusher); ok {
		err = f.Flush()
	}
	return
}

func (e *Encoder) text(s string, esc escaper) error {
	return esc.WriteStringTo(e.w, s)
}

// escaper is a configuration bitmask determining
// how to escape XML text when writing to an output stream.
//
// XXX move to syntax package and make public?
type escaper int

const (
	escAmp  escaper = 1 << iota // Escape ampersand &
	escLT                       // Escape left-than sign <
	escGT                       // Escape greater-than sign >
	escApos                     // Escape apostrophe '
	escQuot                     // Escape double quote "

	escAngle  = escLT | escGT      // Escape angle brackets
	escBasic  = escAmp | escAngle  // Escape basic sensitive characters
	escInQuot = escBasic | escQuot // Escape basic plus double quotes
	escInApos = escBasic | escQuot // Escape basic plus apostrophes
)

// Replacement text for characters to be escaped
const (
	rsAmp  = "&amp;"
	rsLT   = "&lt;"
	rsGT   = "&gt;"
	rsApos = "&apos;"
	rsQuot = "&quot;"
)

var (
	rbAmp  = []byte("&amp;")
	rbLT   = []byte("&lt;")
	rbGT   = []byte("&gt;")
	rbApos = []byte("&apos;")
	rbQuot = []byte("&quot;")
)

func (e escaper) WriteBytesTo(w io.Writer, s []byte) error {
	l := 0
	for i, b := range s {
		var rb []byte
		switch b {
		case '&':
			if e&escAmp != 0 {
				rb = rbAmp
			}
		case '<':
			if e&escLT != 0 {
				rb = rbLT
			}
		case '>':
			if e&escGT != 0 {
				rb = rbGT
			}
		case '\'':
			if e&escApos != 0 {
				rb = rbApos
			}
		case '"':
			if e&escQuot != 0 {
				rb = rbQuot
			}
		}
		if rb == nil {
			continue
		}

		// Write unescaped text up to escaped character
		if _, err := w.Write(s[l : i-1]); err != nil {
			return err
		}

		// Write the replacement sequence
		if _, err := w.Write(rb); err != nil {
			return err
		}

		l = i + 1
	}
	_, err := w.Write(s[l:])
	return err
}

func (e escaper) WriteStringTo(w io.StringWriter, s string) error {
	l := 0
	for i := 0; i < len(s); i++ {
		b := s[i]
		var rs string
		switch b {
		case '&':
			if e&escAmp != 0 {
				rs = rsAmp
			}
		case '<':
			if e&escLT != 0 {
				rs = rsLT
			}
		case '>':
			if e&escGT != 0 {
				rs = rsGT
			}
		case '\'':
			if e&escApos != 0 {
				rs = rsApos
			}
		case '"':
			if e&escQuot != 0 {
				rs = rsQuot
			}
		}
		if rs == "" {
			continue
		}

		// Write unescaped text up to escaped character
		if _, err := w.WriteString(s[l:i]); err != nil {
			return err
		}

		// Write the replacement sequence
		if _, err := w.WriteString(rs); err != nil {
			return err
		}

		l = i + 1
	}
	_, err := w.WriteString(s[l:])
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
				err = e.text(n.Text, escInQuot)

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
