package minml

import (
	"io"

	"github.com/dedis/matchertext/go/markup/ast"
)

// Parse a MinML stream into an abstract syntax tree (AST) representation.
func Parse(r io.Reader) ([]ast.Node, error) {
	ap := &astParser{}
	ap.p.SetReader(r)

	// Parse the input and build the top-level AST in slice ap.m
	if e := ap.p.ReadAll(ap); e != nil {
		return nil, e
	}
	return ap.m, nil
}

type astParser struct {
	p Parser
	m []ast.Node
	a []ast.Attribute
}

func (ap *astParser) Text(text []byte, raw bool) error {
	ap.m = append(ap.m, ast.Text{Text: string(text), Raw: raw})
	return nil
}

func (ap *astParser) Reference(name []byte) error {
	ap.m = append(ap.m, ast.Reference{string(name)})
	return nil
}

func (ap *astParser) Element(name []byte) error {
	nameStr := string(name)

	// Recursively parse this element
	m, a := ap.m, ap.a
	ap.m, ap.a = nil, nil
	if e := ap.p.ReadElement(name, ap); e != nil {
		return e
	}
	elt := ast.Element{Name: nameStr, Attribs: ap.a, Content: ap.m}
	ap.m, ap.a = append(m, elt), a
	return nil
}

func (ap *astParser) Attribute(name []byte) error {
	nameStr := string(name)

	// Recursively parse the attribute value
	m, a := ap.m, ap.a
	ap.m, ap.a = nil, nil
	if e := ap.p.ReadAttribute(name, ap); e != nil {
		return e
	}
	attr := ast.Attribute{Name: nameStr, Value: ap.m}
	ap.m, ap.a = m, append(a, attr)
	return nil
}

func (ap *astParser) Content() error {

	// Recursively parse the element content into slice ap.m
	return ap.p.ReadContent(ap)
}

func (ap *astParser) Comment(text []byte) error {
	ap.m = append(ap.m, ast.Comment{Text: string(text)})
	return nil
}
