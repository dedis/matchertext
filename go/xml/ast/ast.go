// Package ast provides basic abstract syntax trees for XML
package ast

type Node interface {
}

type Text struct {
	Text string
}

type Reference struct {
	Name string
}

type Element struct {
	Name    string
	Attribs []Attribute
	Content []Node
}

type Attribute struct {
	Name  string
	Value []Node
}

// Compare AST nodes n1 and n2 and all their descendants for deep equality.
func DeepEqual(n1, n2 Node) bool {
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
			if !DeepEqual(n1.Attribs[i], n2.Attribs[i]) {
				return false
			}
		}
		for i := 0; i < len(n1.Content); i++ {
			if !DeepEqual(n1.Content[i], n2.Content[i]) {
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
			if !DeepEqual(n1.Value[i], n2.Value[i]) {
				return false
			}
		}
	}
	return true
}
