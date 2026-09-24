package minml

import (
	"strconv"

	"github.com/dedis/matchertext/go/markup/ast"
	"github.com/dedis/matchertext/go/markup/html"
	"github.com/dedis/matchertext/go/markup/xml"
)

type Transformer = ast.Transformer

// EntityTransformer is an optional ast.Transformer
// that converts character references into UTF-8 characters:
// standard HTML named entities, MinML symbolic entities,
// and numeric references #N or #xH to valid XML characters.
// Any other reference becomes the literal text [name] it was written as.
var EntityTransformer = eTransform{}

type eTransform struct{}

func (_ eTransform) Transform(ns []ast.Node) ([]ast.Node, error) {
	for i, n := range ns {
		if ref, ok := n.(ast.Reference); ok {
			name := ref.Reference()
			s, ok := LookupReference(name)
			if !ok {
				s = "[" + name + "]"
			}
			ns[i] = ast.NewText(s)
		}
	}
	return ns, nil
}

// LookupReference returns the characters that reference name stands for,
// or false if name is not a reference that EntityTransformer resolves.
func LookupReference(name string) (string, bool) {
	if s, ok := html.Entity[name]; ok {
		return s, true
	}
	if s, ok := Entity[name]; ok {
		return s, true
	}
	if len(name) < 2 || name[0] != '#' {
		return "", false
	}
	digits, base := name[1:], 10
	if digits[0] == 'x' {
		digits, base = digits[1:], 16
	}
	n, err := strconv.ParseUint(digits, base, 32)
	if err != nil || !xml.IsChar(rune(n)) {
		return "", false
	}
	return string(rune(n)), true
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
