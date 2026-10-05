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
	text := hoverText(m.Kind, d.Text[m.Start:m.End], d.Doctype(d.Text) == "xml")
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
// The descriptions follow what the MinML converter does: to XML if xml is set, else to HTML.
func hoverText(k Kind, src string, xml bool) string {
	switch k {
	case KindTag:
		switch src {
		case `"`, "'":
			return "**Quotation** — `" + src + "[...]`\n\nThe content is MinML markup, enclosed in directed quotation marks."
		}
		if info, ok := HTMLElements[src]; ok && !xml {
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
		if xml {
			return "**Comment** — `-[...]`\n\nConverted to an XML comment."
		}
		return "**Comment** — `-[...]`\n\nConverted to an HTML comment."

	case KindRaw:
		if xml {
			return "**Raw text** — `+[...]`\n\nThe content is not parsed as MinML; it is written as a CDATA section."
		}
		return "**Raw text** — `+[...]`\n\nThe content is not parsed as MinML; it is written as escaped text."

	case KindDoctype:
		if xml {
			return "**Document type** — `![xml]`\n\nThe document converts to XML, after the declaration `<?xml version=\"1.0\" encoding=\"UTF-8\"?>`."
		}
		return "**Document type** — `![html]`\n\nThe document converts to HTML, after `<!DOCTYPE html>`."
	}
	return ""
}
