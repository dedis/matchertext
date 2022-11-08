package minml

import (
	"github.com/dedis/matchertext/go/markup/ast"
)

// The following are convenience constructor functions for AST nodes.

func aText(s string) ast.Text {
	return ast.NewText(s)
}

func aRawText(s string) ast.Text {
	return ast.NewRawText(s)
}

func aComment(s string) ast.Comment {
	return ast.NewComment(s)
}

func aRef(name string) ast.Reference {
	return ast.NewReference(name)
}

func aAttr(name string, ns ...ast.Node) ast.Attribute {
	return ast.NewAttribute(name, ns...)
}

func aElem(name string, ns ...ast.Node) ast.Element {
	return ast.NewElement(name, ns...)
}
