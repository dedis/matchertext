package lsp

import (
	"fmt"

	"github.com/dedis/matchertext/go/markup/minml"
	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
)

func (s *Server) Hover(_ *glsp.Context, params *protocol.HoverParams) (*protocol.Hover, error) {
	d := s.Store.Get(params.TextDocument.URI)
	if d == nil {
		return nil, nil
	}
	i := d.markAt(d.Offset(params.Position))
	if i < 0 {
		return nil, nil
	}
	m := d.Marks[i]
	text := hoverText(m.Kind, d.Text[m.Start:m.End])
	if text == "" {
		return nil, nil
	}
	r := d.Range(m.Start, m.End)
	return &protocol.Hover{
		Contents: protocol.MarkupContent{Kind: protocol.MarkupKindMarkdown, Value: text},
		Range:    &r,
	}, nil
}

// hoverText describes the construct of kind k whose source is src.
// The descriptions follow what the MinML to HTML converter does.
func hoverText(k Kind, src string) string {
	switch k {
	case KindTag:
		switch src {
		case `"`, "'":
			return "**Quotation** — `" + src + "[...]`\n\nThe content is MinML markup, enclosed in directed quotation marks."
		}
		if info, ok := HTMLElements[src]; ok {
			return fmt.Sprintf("### `%s`\n\n%s", src, info.Description)
		}
		return fmt.Sprintf("### `%s`\n\nMinML element, converted to `<%s>`.", src, src)

	case KindAttrName:
		return fmt.Sprintf("### Attribute: `%s`", src)

	case KindReference:
		if s, ok := minml.LookupReference(src[1 : len(src)-1]); ok {
			return fmt.Sprintf("**Character reference** `%s` → `%s`", src, s)
		}
		return fmt.Sprintf("`%s` is not a character reference; it is converted to the literal text.", src)

	case KindComment:
		return "**Comment** — `-[...]`\n\nConverted to an HTML comment."

	case KindRaw:
		return "**Raw text** — `+[...]`\n\nThe content is not parsed as MinML; it is written as escaped text."
	}
	return ""
}
