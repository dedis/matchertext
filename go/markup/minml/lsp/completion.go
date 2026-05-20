package lsp

import (
	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
	sitter "github.com/tree-sitter/go-tree-sitter"
)

func (s *Server) Completion(_ *glsp.Context, params *protocol.CompletionParams) (any, error) {
	var items []protocol.CompletionItem
	s.Store.WithDocument(params.TextDocument.URI, func(doc *Document) {
		node := doc.NodeAt(params.Position.Line, params.Position.Character)
		if node == nil {
			return
		}
		items = buildCompletions(node, doc.TextBytes)
	})
	if items == nil {
		items = []protocol.CompletionItem{}
	}
	return items, nil
}

func buildCompletions(node *sitter.Node, src []byte) []protocol.CompletionItem {
	if isInsideVerbatim(node) {
		return nil
	}

	// Attr context: complete named attr_block, or an incomplete one (ERROR containing {).
	if node.Kind() == "attr_name" || isInAttrContext(node) {
		return attrCompletions(node, src)
	}

	// Tag context: the word node is what the grammar produces for partial/complete tag names.
	if node.Kind() == "tag_name" || node.Kind() == "word" {
		return tagCompletions()
	}

	return nil
}

func isInsideVerbatim(node *sitter.Node) bool {
	for n := node; n != nil; n = n.Parent() {
		switch n.Kind() {
		case "comment", "quoted_string", "raw_block", "processing_instruction", "matcher_escape":
			return true
		}
	}
	return false
}

// isInAttrContext returns true when the node is inside an attribute block,
// including incomplete parses where tree-sitter produces an ERROR node instead
// of a proper attr_block because the closing } has not been typed yet.
func isInAttrContext(node *sitter.Node) bool {
	for n := node; n != nil; n = n.Parent() {
		if n.Kind() == "attr_block" {
			return true
		}
		if n.IsError() {
			for i := uint(0); i < n.ChildCount(); i++ {
				if n.Child(i).Kind() == "{" {
					return true
				}
			}
		}
	}
	return false
}

func tagCompletions() []protocol.CompletionItem {
	kind := protocol.CompletionItemKindKeyword
	items := make([]protocol.CompletionItem, 0, len(HTMLElements))
	for tag, info := range HTMLElements {
		t := tag
		desc := info.Description
		items = append(items, protocol.CompletionItem{
			Label:  t,
			Kind:   &kind,
			Detail: &desc,
		})
	}
	return items
}

func attrCompletions(node *sitter.Node, src []byte) []protocol.CompletionItem {
	var attrs []string
	tagName := findEnclosingTagName(node, src)
	if info, ok := HTMLElements[tagName]; ok {
		attrs = info.Attributes
	}
	if len(attrs) == 0 {
		attrs = GlobalAttrs
	}

	kind := protocol.CompletionItemKindProperty
	items := make([]protocol.CompletionItem, 0, len(attrs))
	for _, attr := range attrs {
		a := attr
		k := kind
		items = append(items, protocol.CompletionItem{Label: a, Kind: &k})
	}
	return items
}

// findEnclosingTagName resolves the tag name for the element that owns the
// current attr context. It handles both complete parses (attr_block is a child
// of element) and error-recovery parses (attr block is an ERROR node whose
// preceding sibling in source_file is a word node carrying the tag name).
func findEnclosingTagName(node *sitter.Node, src []byte) string {
	for n := node; n != nil; n = n.Parent() {
		if n.Kind() == "element" {
			if tagNode := n.ChildByFieldName("tag"); tagNode != nil {
				return tagNode.Utf8Text(src)
			}
			return ""
		}
		// Incomplete parse: the { is inside an ERROR node. The tag name is the
		// word node immediately preceding this ERROR in the parent's child list.
		if n.IsError() || n.Kind() == "attr_block" {
			parent := n.Parent()
			if parent == nil {
				return ""
			}
			nStart := n.StartPosition()
			for i := uint(1); i < parent.ChildCount(); i++ {
				child := parent.Child(i)
				cs := child.StartPosition()
				if cs.Row == nStart.Row && cs.Column == nStart.Column {
					prev := parent.Child(i - 1)
					if prev.Kind() == "word" || prev.Kind() == "tag_name" {
						return prev.Utf8Text(src)
					}
					return ""
				}
			}
			return ""
		}
	}
	return ""
}
