package minml

import (
	"github.com/dedis/matchertext/go/markup/ast"
	"github.com/dedis/matchertext/go/markup/html"
)

type Transformer = ast.Transformer

// EntityTransformer is an optional ast.Transformer
// that recognizes and converts both standard HTML named character entities,
// and the MinML symbolic character entities, into UTF-8 characters.
var EntityTransformer = eTransform{}

type eTransform struct{}

func (_ eTransform) Transform(ns []ast.Node) ([]ast.Node, error) {
	for i, n := range ns {
		if ref, ok := n.(ast.Reference); ok {

			// XXX handle numeric entities as well?

			// Apply the standard HTML named entities
			s, ok := html.Entity[ref.Reference()]
			if !ok {
				// Then apply the MinML symbolic entities
				s, ok = Entity[ref.Reference()]
			}
			if ok {
				ns[i] = ast.NewText(s)
			}
		}
	}
	return ns, nil
}

// QuoteTransformer is an optional ast.Transformer
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
			name, _, content := elt.Element()

			// Recognize single or double quotation elements
			var o, c string
			if name == "'" {
				o, c = "\u2018", "\u2019"
			} else if name == "\"" {
				o, c = "\u201C", "\u201D"
			}
			if o != "" {

				// Start a new node slice if necessary
				if nsn == nil {
					nsn = append(nsn, ns[:i]...)
				}

				// Append quote-delimited element content
				nsn = append(nsn, ast.NewText(o))
				nsn = append(nsn, content...)
				nsn = append(nsn, ast.NewText(c))
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

// MatcherTransformer is an optional ast.Transformer
// that converts unmatched matchers in literal text
// into MinML-style matcher character references.
var MatcherTransformer = &ast.MatcherTransformer{Escaper: minmlEscaper}

func minmlEscaper(b byte) string {
	switch b {
	case '(':
		return "(<)"
	case ')':
		return "(>)"
	case '[':
		return "[<]"
	case ']':
		return "[>]"
	case '{':
		return "{<}"
	case '}':
		return "{>}"
	default:
		panic("Escaper argument must be a matcher")
	}
}
