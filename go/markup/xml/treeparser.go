package xml

import (
	"encoding/xml"
	"fmt"
	"io"
	"strings"

	"github.com/dedis/matchertext/go/markup/ast"
)

// TreeParser parses an XML stream into an abstract syntax tree (AST).
// It uses raw XML tokenization to preserve original element and attribute
// names including namespace prefixes.
type TreeParser struct {
	dec *xml.Decoder
}

// NewTreeParser creates a TreeParser to parse XML from r.
func NewTreeParser(r io.Reader) *TreeParser {
	dec := xml.NewDecoder(r)
	dec.Strict = false
	return &TreeParser{dec: dec}
}

// ParseAST parses the XML input and returns the top-level AST nodes.
func (p *TreeParser) ParseAST() ([]ast.Node, error) {
	return p.parseContent(false, "")
}

// parseContent reads AST nodes from the XML stream.
// When insideElement is true it stops at the next EndElement, which it consumes.
func (p *TreeParser) parseContent(insideElement bool, elemName string) ([]ast.Node, error) {
	var nodes []ast.Node
	for {
		tok, err := p.dec.RawToken()
		if err == io.EOF {
			if insideElement {
				return nil, fmt.Errorf("unexpected EOF inside element <%s>", elemName)
			}
			return nodes, nil
		}
		if err != nil {
			return nil, err
		}

		switch t := tok.(type) {
		case xml.CharData:
			if s := string(t); s != "" {
				nodes = append(nodes, ast.NewText(s))
			}

		case xml.Comment:
			nodes = append(nodes, ast.NewComment(string(t)))

		case xml.StartElement:
			elt, err := p.parseElement(t)
			if err != nil {
				return nil, err
			}
			nodes = append(nodes, elt)

		case xml.EndElement:
			if !insideElement {
				return nil, fmt.Errorf("unexpected end element </%s>", rawXMLName(t.Name))
			}
			return nodes, nil

		case xml.ProcInst, xml.Directive:
			// skip processing instructions and DOCTYPE declarations
		}
	}
}

func (p *TreeParser) parseElement(start xml.StartElement) (ast.Node, error) {
	name := rawXMLName(start.Name)

	var ns []ast.Node
	for _, a := range start.Attr {
		attrName := rawXMLName(a.Name)
		if isXMLNS(attrName) {
			continue // namespace declarations have no meaning in MinML
		}
		ns = append(ns, ast.NewAttribute(attrName, ast.NewText(a.Value)))
	}

	content, err := p.parseContent(true, name)
	if err != nil {
		return nil, err
	}

	return ast.NewElement(name, append(ns, content...)...), nil
}

// rawXMLName formats an xml.Name as it appeared in the source.
// With RawToken(), Name.Space holds the namespace prefix (not the URI).
func rawXMLName(name xml.Name) string {
	if name.Space != "" {
		return name.Space + ":" + name.Local
	}
	return name.Local
}

// isXMLNS reports whether an attribute name is a namespace declaration.
func isXMLNS(name string) bool {
	return name == "xmlns" || strings.HasPrefix(name, "xmlns:")
}
