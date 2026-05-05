package lsp

import (
	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
)

func (s *Server) Completion(context *glsp.Context, params *protocol.CompletionParams) (any, error) {
	doc, ok := s.Store.Get(params.TextDocument.URI)
	if !ok {
		return nil, nil
	}

	node := doc.NodeAt(params.Position.Line, params.Position.Character)
	if node == nil {
		return nil, nil
	}

	items := []protocol.CompletionItem{}

	kindTag := protocol.CompletionItemKindKeyword
	kindAttr := protocol.CompletionItemKindProperty

	// Check if we are inside a tag name
	if node.Kind() == "tag_name" || (node.Parent() != nil && node.Parent().Kind() == "element") {
		for tag, info := range HTMLElements {
			items = append(items, protocol.CompletionItem{
				Label:  tag,
				Kind:   &kindTag,
				Detail: &info.Description,
			})
		}
	} else if node.Kind() == "attr_name" || (node.Parent() != nil && node.Parent().Kind() == "attribute") {
		// Try to find the tag name to provide relevant attributes
		parent := node.Parent()
		for parent != nil && parent.Kind() != "element" {
			parent = parent.Parent()
		}

		var attrs []string
		if parent != nil {
			tagNode := parent.ChildByFieldName("tag")
			if tagNode != nil {
				tagName := tagNode.Utf8Text([]byte(doc.Text))
				if info, ok := HTMLElements[tagName]; ok {
					attrs = info.Attributes
				}
			}
		}

		// Fallback to all common attributes if tag not found
		if len(attrs) == 0 {
			attrs = []string{"class", "id", "style", "title", "href", "src"}
		}

		for _, attr := range attrs {
			items = append(items, protocol.CompletionItem{
				Label: attr,
				Kind:  &kindAttr,
			})
		}
	}

	return items, nil
}
