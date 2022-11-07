// Package ast provides basic abstract syntax trees for *ML markup languages
package ast

// Node represents an arbitrary AST node defined by this package.
//
// Clone() yields a shallow copy of the Node of the same underlying type.
// The contents of indirect state such as slices in the clone
// may subsequently be modified without affecting the original.
//
// Equal() performs a deep comparison for equality with another Node,
// including equality of the underlying type and of all child nodes.
// It is not expected to be particularly efficient,
// and is intended primarily for testing and debugging purposes.
type Node interface {
	Clone() Node     // create an identical copy of this Node
	Equal(Node) bool // deep equality compare with another Node
}

// Text represents a sequence of literal text within a markup stream.
type Text interface {
	Node
	Text() string // the plain markup-free UTF-8 text
}

// RawText extends the Text interface with information about
// whether the text came from a raw text section such as CDATA in XML.
type RawText interface {
	Text
	IsRaw() bool // true if this text came from a raw CDATA section
}

// Reference represents a named or numeric character reference.
// Name does not include the leading & or trailing ; in XML syntax.
type Reference interface {
	Node
	Reference() string // named or numeric character reference
}

// Element represents a markup element
// with a name, optional attributes, and optional markup content.
// The returned slices must not be modified except after Clone().
type Element interface {
	Node
	Element() (name string, attrs []Attribute, content []Node)
}

// Attribute represents a single attribute within an Element,
// containing a name and associated value.
// The value consists only of Text and Reference nodes.
// The returned value slice must not be modified except after Clone().
type Attribute interface {
	Node
	Attribute() (name string, value []Node)
}

// Comment represents an XML comment within a markup stream.
// The Text it contains is normally ignored for processing purposes.
type Comment interface {
	Node
	Comment() string // the text content of the comment
}

type text string

// Create a Text markup node whose text content is string s.
func NewText(s string) text {
	return text(s)
}

func (t text) Text() string {
	return string(t)
}

func (t text) Clone() Node {
	return t // immutable strings are trivial to clone
}

func (t text) Equal(n Node) bool {
	if nt, ok := n.(text); ok {
		return t == nt
	}
	return false
}

type rawtext text

// Create a RawText markup node representing raw text string s.
// IsRaw() returns true in the resulting object.
func NewRawText(s string) RawText {
	return rawtext(s)
}

func (r rawtext) Text() string {
	return string(r)
}

func (r rawtext) Clone() Node {
	return r // immutable strings are trivial to clone
}

func (r rawtext) Equal(n Node) bool {
	if nr, ok := n.(rawtext); ok {
		return r == nr
	}
	return false
}

func (r rawtext) IsRaw() bool {
	return true
}

type ref string

// Create a named or numeric character reference from string s
func NewReference(s string) Reference {
	return ref(s)
}

func (r ref) Reference() string {
	return string(r)
}

func (r ref) Clone() Node {
	return r // immutable strings are trivial to clone
}

func (r ref) Equal(n Node) bool {
	if nr, ok := n.(ref); ok {
		return r == nr
	}
	return false
}

type attr struct {
	n string // name
	v []Node // value
}

// Create an attribute node with the given name and content nodes ns
func NewAttribute(name string, ns ...Node) Attribute {
	return attr{n: name, v: ns}
}

func (a attr) Attribute() (string, []Node) {
	return a.n, a.v
}

func (a attr) Clone() Node {
	nv := make([]Node, len(a.v))
	copy(nv, a.v)
	return attr{a.n, nv}
}

func (a attr) Equal(n Node) bool {
	if na, ok := n.(attr); ok {
		return a.n == na.n && Equal(a.v, na.v)
	}
	return false
}

type element struct {
	n string      // name
	a []Attribute // attributes
	c []Node      // content
}

// Create an element node with the given name and
// with attributes and content nodes from slice ns.
// All Attribute nodes must come first within ns.
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
	return element{n: name, a: as, c: ns}
}

func (e element) Element() (string, []Attribute, []Node) {
	return e.n, e.a, e.c
}

func (e element) Clone() Node {
	na := make([]Attribute, len(e.a))
	copy(na, e.a)
	nc := make([]Node, len(e.c))
	copy(nc, e.c)
	return element{n: e.n, a: na, c: nc}
}

func (e element) Equal(n Node) bool {
	if ne, ok := n.(element); ok {
		if e.n != ne.n ||
			len(e.a) != len(ne.a) ||
			len(e.c) != len(ne.c) {
			return false
		}
		for i := range e.a {
			if !e.a[i].Equal(ne.a[i]) {
				return false
			}
		}
		for i := range e.c {
			if !e.c[i].Equal(ne.c[i]) {
				return false
			}
		}
		return true
	}
	return false
}

type comment string

// Create a comment node containing text s
func NewComment(s string) comment {
	return comment(s)
}

func (c comment) Comment() string {
	return string(c)
}

func (c comment) Clone() Node {
	return c
}

func (c comment) Equal(n Node) bool {
	if nc, ok := n.(comment); ok {
		return c == nc
	}
	return false
}

// Compare AST nodes n1 and n2 and all their descendants for deep equality.
func Equal(n1, n2 []Node) bool {
	if len(n1) != len(n2) {
		return false
	}
	for i := range n1 {
		if !n1[i].Equal(n2[i]) {
			return false
		}
	}
	return true
}
