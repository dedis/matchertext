package lsp

import (
	"fmt"

	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
	sitter "github.com/tree-sitter/go-tree-sitter"
)

func (s *Server) Hover(context *glsp.Context, params *protocol.HoverParams) (*protocol.Hover, error) {
	doc, ok := s.Store.Get(params.TextDocument.URI)
	if !ok {
		return nil, nil
	}

	node := doc.NodeAt(params.Position.Line, params.Position.Character)
	if node == nil {
		return nil, nil
	}

	text := hoverText(node, doc.Text)
	if text == "" {
		return nil, nil
	}

	return &protocol.Hover{
		Contents: protocol.MarkupContent{
			Kind:  protocol.MarkupKindMarkdown,
			Value: text,
		},
	}, nil
}

func hoverText(node *sitter.Node, src string) string {
	raw := node.Utf8Text([]byte(src))

	switch node.Kind() {
	case "tag_name":
		if info, ok := HTMLElements[raw]; ok {
			return fmt.Sprintf("### %s\n\n%s", raw, info.Description)
		}
		return fmt.Sprintf("### %s\n\nMinML element tag.", raw)

	case "attr_name":
		return fmt.Sprintf("### Attribute: `%s`", raw)

	case "comment":
		return "**Comment** — `-[...]`\n\nContent between the brackets is ignored by the renderer."

	case "raw_block":
		return "**Raw block** — `+[...]`\n\nContent between the brackets is passed through to the output verbatim, without MinML processing."

	case "quoted_string":
		return "**Quoted string** — `\"[...]`\n\nBracket characters inside are treated as literal text, not as element delimiters."

	case "processing_instruction":
		return "**Processing instruction** — `?[...]`\n\nAn implementation-specific directive. Semantics depend on the processor."

	case "named_ref":
		return fmt.Sprintf("**Named character reference** — `[%s]`\n\nInserts the Unicode character identified by this name.", raw)

	case "decimal_ref":
		return fmt.Sprintf("**Decimal character reference** — `[%s]`\n\nInserts the Unicode code point (decimal).", raw)

	case "hex_ref":
		return fmt.Sprintf("**Hex character reference** — `[%s]`\n\nInserts the Unicode code point (hexadecimal).", raw)

	case "matcher_escape":
		return "**Matcher escape** — `[[<]]` `[[>]]` `[(<)]` `[(>)]`\n\nInserts a literal bracket character that would otherwise be a structural delimiter."
	}

	return ""
}
