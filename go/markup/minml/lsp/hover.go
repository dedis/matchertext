package lsp

import (
	"fmt"

	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
	sitter "github.com/tree-sitter/go-tree-sitter"
)

func (s *Server) Hover(context *glsp.Context, params *protocol.HoverParams) (*protocol.Hover, error) {
	var result *protocol.Hover
	s.Store.WithDocument(params.TextDocument.URI, func(doc *Document) {
		node := doc.NodeAt(params.Position.Line, params.Position.Character)
		if node == nil {
			return
		}
		text := hoverText(node, doc.TextBytes)
		if text == "" {
			return
		}
		start := node.StartPosition()
		end := node.EndPosition()
		result = &protocol.Hover{
			Contents: protocol.MarkupContent{
				Kind:  protocol.MarkupKindMarkdown,
				Value: text,
			},
			Range: &protocol.Range{
				Start: protocol.Position{Line: uint32(start.Row), Character: uint32(start.Column)},
				End:   protocol.Position{Line: uint32(end.Row), Character: uint32(end.Column)},
			},
		}
	})
	return result, nil
}

func hoverText(node *sitter.Node, src []byte) string {
	raw := node.Utf8Text(src)

	switch node.Kind() {
	case "tag_name":
		if info, ok := HTMLElements[raw]; ok {
			return fmt.Sprintf("### `%s`\n\n%s", raw, info.Description)
		}
		return fmt.Sprintf("### `%s`\n\nMinML element tag.", raw)

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
		return "**Matcher escape**\n\nInserts a literal bracket that would otherwise be a structural delimiter.\n\n```\n[[<]]  [[>]]  [(<)]  [(>)]\n```"
	}

	return ""
}
