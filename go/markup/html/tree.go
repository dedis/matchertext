package html

import (
	"fmt"
	"io"
	"strings"

	"github.com/dedis/matchertext/go/internal/util"
	"github.com/dedis/matchertext/go/markup/ast"
	"github.com/dedis/matchertext/go/markup/xml"
)

type TreeWriter struct {
	w     util.AtomWriter
	depth int    // number of elements enclosing the nodes being written
	space string // how the HTML parser reads the nodes being written: see childSpace
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

		case ast.Doctype:
			err = e.doctype(n.Doctype(), i)

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

// Write the HTML document type, which only the first node of the document may declare.
func (e *TreeWriter) doctype(kind string, i int) error {
	if kind != "html" || i != 0 || e.depth != 0 {
		return encError(fmt.Sprintf("document type %q is not html, or not the first node", kind))
	}
	_, err := e.w.WriteString("<!DOCTYPE html>")
	return err
}

func (e *TreeWriter) text(s string, esc xml.Escaper) error {
	return esc.WriteStringTo(e.w, s)
}

// Write a reference to XML output.
// A name that is not a valid reference could break or inject markup, so it is an error.
func (e *TreeWriter) reference(name string) error {
	if !xml.IsReference([]byte(name)) {
		return encError(fmt.Sprintf("invalid character reference %q", name))
	}

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

	// recursively write the element content,
	// or the text of a raw text element, which the HTML parser does not unescape
	space := e.space
	ns := elementSpace(space, name)
	e.space = childSpace(ns, name, attrs)
	e.depth++
	if raw := rawTextElement(name); raw != "" && ns == "" {
		err = e.rawText(raw, content)
	} else {
		err = e.WriteAST(content)
	}
	e.depth--
	e.space = space
	if err != nil {
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

// elementSpace returns the namespace that the HTML parser gives to element name,
// "" for HTML, "svg" for SVG, or "math" for MathML, when it reads the element in space.
// It ignores the HTML elements that end SVG or MathML content, such as p.
// https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inforeign
func elementSpace(space, name string) string {
	switch space {
	case "":
		if strings.EqualFold(name, "svg") || strings.EqualFold(name, "math") {
			return strings.ToLower(name)
		}
	case "annotation-xml":
		if strings.EqualFold(name, "svg") {
			return "svg"
		}
		return "math"
	}
	return space
}

// childSpace returns how the HTML parser reads the content of element name in namespace ns:
// "" by the HTML rules, "svg" or "math" as SVG or MathML content,
// or "annotation-xml" as MathML content in which svg starts SVG content.
// https://html.spec.whatwg.org/multipage/parsing.html#html-integration-point
func childSpace(ns, name string, attrs []ast.Attribute) string {
	switch {
	case ns == "svg" && (strings.EqualFold(name, "foreignObject") ||
		strings.EqualFold(name, "desc") || strings.EqualFold(name, "title")):
		return ""
	case ns == "math" && (strings.EqualFold(name, "mi") || strings.EqualFold(name, "mo") ||
		strings.EqualFold(name, "mn") || strings.EqualFold(name, "ms") || strings.EqualFold(name, "mtext")):
		return ""
	case ns == "math" && strings.EqualFold(name, "annotation-xml"):
		if htmlEncoding(attrs) {
			return ""
		}
		return "annotation-xml"
	}
	return ns
}

// htmlEncoding reports whether attrs has an encoding attribute
// that makes a MathML annotation-xml element hold HTML.
func htmlEncoding(attrs []ast.Attribute) bool {
	for _, a := range attrs {
		name, value := a.Attribute()
		if !strings.EqualFold(name, "encoding") {
			continue
		}
		var b strings.Builder
		for _, n := range value {
			if t, ok := n.(ast.Text); ok {
				b.WriteString(t.Text())
			}
		}
		v := b.String()
		return strings.EqualFold(v, "text/html") || strings.EqualFold(v, "application/xhtml+xml")
	}
	return false
}

// rawTextElement returns "script" or "style" if name is one of these raw text elements
// in any case, or else "".
// https://html.spec.whatwg.org/multipage/syntax.html#raw-text-elements
func rawTextElement(name string) string {
	for _, raw := range [...]string{"script", "style"} {
		if strings.EqualFold(name, raw) {
			return raw
		}
	}
	return ""
}

// Write the content of raw text element name, which must be text
// that the HTML parser reads up to the end tag that follows it.
func (e *TreeWriter) rawText(name string, content []ast.Node) error {
	var b strings.Builder
	for _, n := range content {
		t, ok := n.(ast.Text)
		if !ok {
			return encError(fmt.Sprintf("%s element content must be text only", name))
		}
		b.WriteString(t.Text())
	}
	s := b.String()
	if endsEarly(name, s) {
		return encError(fmt.Sprintf("%s element content would end the element early", name))
	}
	_, err := e.w.WriteString(s)
	return err
}

// endsEarly reports whether the HTML parser, reading text s as the content of
// raw text element name, would not end the element at the end tag after s:
// because s contains an end tag of the element, or because s is a script
// that leaves the parser in the state in which that end tag does not end the script.
// https://html.spec.whatwg.org/multipage/parsing.html#script-data-state
func endsEarly(name, s string) bool {
	const (
		data          = iota // an end tag ends the element
		escaped              // after <!--: an end tag ends the element
		doubleEscaped        // after <!--<script: an end tag returns to escaped
	)
	state := data
	for i := 0; i < len(s); i++ {
		switch {
		case state != doubleEscaped && tagAt(s, i, "</", name):
			return true
		case name != "script":
		case state == data && strings.HasPrefix(s[i:], "<!--"):
			state = escaped
			i++ // the dashes can also end the escape, as in <!-->
		case state != data && strings.HasPrefix(s[i:], "-->"):
			state = data
			i += 2
		case state == escaped && tagAt(s, i, "<", "script"):
			state = doubleEscaped
		case state == doubleEscaped && tagAt(s, i, "</", "script"):
			state = escaped
		}
	}
	return state == doubleEscaped
}

// tagAt reports whether s has at offset i a tag start: prefix, then name in any case,
// then a character that ends a tag name. At the end of s, the end tag that follows
// s would continue the tag name, so that is not a tag start.
func tagAt(s string, i int, prefix, name string) bool {
	j := i + len(prefix) + len(name)
	return j < len(s) && strings.HasPrefix(s[i:], prefix) &&
		strings.EqualFold(s[i+len(prefix):j], name) && strings.IndexByte("\t\n\f\r />", s[j]) >= 0
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
