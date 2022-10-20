package minml

import (
	"github.com/dedis/matchertext/go/markup/ast"
	"github.com/dedis/matchertext/go/markup/html"
)

// Transformer takes a slice of AST nodes from src,
// performs some transformation on any or all of them,
// and returns the resulting transformed slice of nodes.
//
// A particular Transformer typically recognizes and transforms
// only certain selected nodes, leaving others unmodified.
// A transformer may modify the existing slice in place,
// may insert and remove nodes,
// and may return either the existing slice or a new slice.
//
// A Transformer must transform Attribute nodes only into Attribute nodes.
type Transformer interface {
	Transform(ns []ast.Node) ([]ast.Node, error)
}

// EntityTransformer is an optional Transformer
// that recognizes and converts both standard HTML named character entities,
// and the MinML symbolic character entities, into UTF-8 characters.
var EntityTransformer = eTransform{}

type eTransform struct{}

func (_ eTransform) Transform(ns []ast.Node) ([]ast.Node, error) {
	for i, n := range ns {
		if ref, ok := n.(ast.Reference); ok {

			// XXX handle numeric entities as well?

			// Apply the standard HTML named entities
			s, ok := html.Entity[ref.Name]
			if !ok {
				// Then apply the MinML symbolic entities
				s, ok = Entity[ref.Name]
			}
			if ok {
				ns[i] = ast.Text{s, false}
			}
		}
	}
	return ns, nil
}

// QuoteTransformer is an optional Transformer
// that converts MinML single-quoted string elements '[...]
// and double-quoted string elements "[...]
// into normal character sequences delimited by
// the appropriate directed quote characters.
var QuoteTransformer = qTransform{}

type qTransform struct{}

func (_ qTransform) Transform(ns []ast.Node) ([]ast.Node, error) {

	// If we find any quote transformations to perform,
	// we will build a new markup node slice in nsn.
	var nsn []ast.Node
	for i, n := range ns {
		if elt, ok := n.(ast.Element); ok {

			// Recognize single or double quotation elements
			var o, c string
			if elt.Name == "'" {
				o, c = "\u2018", "\u2019"
			} else if elt.Name == "\"" {
				o, c = "\u201C", "\u201D"
			}
			if o != "" {

				// Start a new node slice if necessary
				if nsn == nil {
					nsn = append(nsn, ns[:i]...)
				}

				// Append quote-delimited element content
				nsn = append(nsn, ast.Text{o, false})
				nsn = append(nsn, elt.Content...)
				nsn = append(nsn, ast.Text{c, false})
				continue
			}
		}

		// Append n to new slice only if we have started building one
		if nsn != nil {
			nsn = append(nsn, n)
		}
	}
	if nsn != nil {
		return nsn, nil
	}
	return ns, nil
}
