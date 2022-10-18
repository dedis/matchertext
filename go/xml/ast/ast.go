// Package ast provides basic abstract syntax trees for XML
package ast

type Node interface {
}

type Text struct {
	Text string // the plain markup-free UTF-8 text
	Raw  bool   // true if this text came from a raw CDATA section
}

type Reference struct {
	Name string // XML name of character reference
}

type Element struct {
	Name    string      // XML name of element
	Attribs []Attribute // Element attributes if any
	Content []Node      // Element markup content if any
}

type Attribute struct {
	Name  string
	Value []Node
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
	default:
		return false
	}
	return true
}
