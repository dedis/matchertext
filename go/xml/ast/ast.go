// Package ast provides basic abstract syntax trees for XML
package ast

// Node represents an arbitrary AST node defined by this package.
type Node interface {
}

// Text represents a sequence of literal text within a markup stream.
// If Raw is true, the text represents raw character data (CDATA).
type Text struct {
	Text string // the plain markup-free UTF-8 text
	Raw  bool   // true if this text came from a raw CDATA section
}

// Reference represents a named or numeric character reference.
// Name does not include the leading & or trailing ; in XML syntax.
type Reference struct {
	Name string // XML name of character reference
}

// Element represents a markup element with tag Name,
// attributes in Attribs, and containing markup Content.
type Element struct {
	Name    string      // XML name of element
	Attribs []Attribute // Element attributes if any
	Content []Node      // Element markup content if any
}

// Attribute represents a single attribute within an Element.
// Name is an XML name.
// Value consists only of Text and Reference nodes.
type Attribute struct {
	Name  string
	Value []Node
}

// Comment represents an XML comment within a markup stream.
// The Text it contains is normally ignored for processing purposes.
type Comment struct {
	Text string // the text content of the comment
}

// Create a text markup node containing text s.
// If raw is true, the node represents raw character data (CDATA).
func NewText(s string, raw bool) Text {
	return Text{Text: s, Raw: raw}
}

// Create a comment node containing text s
func NewComment(s string) Comment {
	return Comment{Text: s}
}

// Create a named or numeric character reference from s
func NewReference(name string) Reference {
	return Reference{Name: name}
}

// Create an attribute node with the given name and content nodes ns
func NewAttribute(name string, ns ...Node) Attribute {
	return Attribute{Name: name, Value: ns}
}

// Create an element node with the given name and
// with attributes and content nodes from slice ns.
// All Attribute nodes must be first within ns.
func NewElement(name string, ns ...Node) Element {
	var as []Attribute
	for len(ns) > 0 {
		if a, ok := ns[0].(Attribute); ok {
			as = append(as, a)
			ns = ns[1:]
		} else {
			break
		}
	}
	return Element{Name: name, Attribs: as, Content: ns}
}

// Compare AST nodes n1 and n2 and all their descendants for deep equality.
func DeepEqual(n1, n2 []Node) bool {
	if len(n1) != len(n2) {
		return false
	}
	for i := range n1 {
		if !deepEqual(n1[i], n2[i]) {
			return false
		}
	}
	return true
}

func deepEqual(n1, n2 Node) bool {
	switch n1 := n1.(type) {
	case Text:
		if n2, ok := n2.(Text); !ok || n1 != n2 {
			return false
		}
	case Reference:
		if n2, ok := n2.(Reference); !ok || n1 != n2 {
			return false
		}
	case Element:
		n2, ok := n2.(Element)
		if !ok || n1.Name != n2.Name ||
			len(n1.Attribs) != len(n2.Attribs) ||
			len(n1.Content) != len(n2.Content) {
			return false
		}
		for i := 0; i < len(n1.Attribs); i++ {
			if !deepEqual(n1.Attribs[i], n2.Attribs[i]) {
				return false
			}
		}
		for i := 0; i < len(n1.Content); i++ {
			if !deepEqual(n1.Content[i], n2.Content[i]) {
				return false
			}
		}
	case Attribute:
		n2, ok := n2.(Attribute)
		if !ok || n1.Name != n2.Name ||
			len(n1.Value) != len(n2.Value) {
			return false
		}
		for i := 0; i < len(n1.Value); i++ {
			if !deepEqual(n1.Value[i], n2.Value[i]) {
				return false
			}
		}
	case Comment:
		if n2, ok := n2.(Comment); !ok || n1 != n2 {
			return false
		}
	default:
		return false
	}
	return true
}
