package minml

import (
	"io"

	"github.com/dedis/matchertext/go/markup/ast"
)

// XXX rename this to TreeParser instead of Decoder, perhaps,
// since a Decoder in Go traditionally decodees into Go objects?

// A Decoder parses a MinML stream into an abstract syntax tree (AST).
type Decoder struct {
	ap astParser
}

// NewDecoder creates a Decoder to parse input r.
func NewDecoder(r io.Reader) *Decoder {
	d := &Decoder{}
	d.ap.p.SetReader(r)
	return d
}

// Parse a MinML stream into an abstract syntax tree (AST) representation.
func (d *Decoder) Decode() ([]ast.Node, error) {
	return d.ap.decode()
}

// Add t to the list of transformers to be invoked
// on every new AST node decoded from the input stream, and returns d.
// Multiple transformers are applied in the order they were added.
func (d *Decoder) WithTransformer(t Transformer) *Decoder {
	d.ap.t = append(d.ap.t, t)
	return d
}

// We use this private internal struct to avoid exposing
// the parsing callbacks below in the public Decoder type.
type astParser struct {
	p Parser        // the underlying MinML parser
	m []ast.Node    // slice of markup nodes being built
	a []ast.Node    // slice of attribute nodes being built
	t []Transformer // transformers to transform new nodes
}

func (ap *astParser) decode() ([]ast.Node, error) {

	// Parse the input and build the top-level AST in slice ap.m
	ap.m = nil
	if e := ap.p.ReadAll(ap); e != nil {
		return nil, e
	}

	// Apply any transformers to the resulting markup
	ns, err := ap.xform(ap.m)
	if err != nil {
		return nil, err
	}

	return ns, nil
}

func (ap *astParser) Text(text []byte, raw bool) error {

	// Create a new Text node
	ap.m = append(ap.m, ast.Text{Text: string(text), Raw: raw})
	return nil
}

func (ap *astParser) Reference(name []byte) error {

	// Create a new Reference node
	ap.m = append(ap.m, ast.Reference{string(name)})
	return nil
}

func (ap *astParser) Element(name []byte) error {
	nameStr := string(name)

	// Save the current node slice under construction
	om, oa := ap.m, ap.a
	ap.m, ap.a = nil, nil

	// Recursively parse this element
	if e := ap.p.ReadElement(name, ap); e != nil {
		return e
	}

	// Transform the element's attributes as appropriate
	ans, err := ap.xform(ap.a)
	if err != nil {
		return err
	}
	as := make([]ast.Attribute, len(ans))
	for i, an := range ans {
		a, ok := an.(ast.Attribute)
		if !ok {
			panic("transformed attribute into non-attribute")
		}
		as[i] = a
	}

	// Transform the element's content markup as appropriate
	ms, err := ap.xform(ap.m)
	if err != nil {
		return err
	}

	// Create the new resulting Element node
	elt := ast.Element{Name: nameStr, Attribs: as, Content: ms}
	ap.m, ap.a = append(om, elt), oa
	return nil
}

func (ap *astParser) Attribute(name []byte) error {
	nameStr := string(name)

	om, oa := ap.m, ap.a
	ap.m, ap.a = nil, nil

	// Recursively parse the attribute value
	if e := ap.p.ReadAttribute(name, ap); e != nil {
		return e
	}
	attr := ast.Attribute{Name: nameStr, Value: ap.m}

	ap.m, ap.a = om, append(oa, attr)
	return nil
}

func (ap *astParser) Content() error {

	// Recursively parse the element content into slice ap.m
	return ap.p.ReadContent(ap)
}

func (ap *astParser) Comment(text []byte) error {

	// Create a new Comment node
	ap.m = append(ap.m, ast.Comment{Text: string(text)})
	return nil
}

// Take a newly-produced AST node and apply all appropriate transformers to it,
// returning the resulting list of markup nodes.
func (ap *astParser) xform(ns []ast.Node) ([]ast.Node, error) {
	for _, t := range ap.t {
		nsn, err := t.Transform(ns)
		if err != nil {
			return nil, err
		}
		ns = nsn
	}
	return ns, nil
}
