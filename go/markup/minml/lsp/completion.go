package lsp

import (
	"sort"

	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
)

func (s *Server) Completion(_ *glsp.Context, params *protocol.CompletionParams) (any, error) {
	d := s.Store.Get(params.TextDocument.URI)
	if d == nil {
		return []protocol.CompletionItem{}, nil
	}
	byTrigger := params.Context != nil && params.Context.TriggerKind == protocol.CompletionTriggerKindTriggerCharacter
	return d.completions(d.Offset(params.Position), byTrigger), nil
}

// completions returns attribute names inside an attribute block, outside attribute values,
// and tag names in element content. The '{' trigger character asks only for attribute names.
func (d *Document) completions(o int, byTrigger bool) []protocol.CompletionItem {
	i := sort.Search(len(d.Blocks), func(i int) bool { return d.Blocks[i].Close >= o })
	if i < len(d.Blocks) && d.Blocks[i].Open < o {
		b := &d.Blocks[i]
		for _, v := range b.Values {
			if v[0] < o && o <= v[1] {
				return []protocol.CompletionItem{}
			}
		}
		return attrCompletions(b.Tag)
	}
	if byTrigger {
		return []protocol.CompletionItem{}
	}
	if o > 0 {
		if i := d.markAt(o - 1); i >= 0 {
			switch d.Marks[i].Kind {
			case KindReference, KindComment, KindRaw:
				return []protocol.CompletionItem{}
			}
		}
	}
	return tagItems
}

// tagItems lists the HTML elements sorted by name.
var tagItems = func() []protocol.CompletionItem {
	kind := protocol.CompletionItemKindKeyword
	items := make([]protocol.CompletionItem, 0, len(HTMLElements))
	for tag, info := range HTMLElements {
		items = append(items, protocol.CompletionItem{Label: tag, Kind: &kind, Detail: &info.Description})
	}
	sort.Slice(items, func(i, j int) bool { return items[i].Label < items[j].Label })
	return items
}()

func attrCompletions(tag string) []protocol.CompletionItem {
	attrs := GlobalAttrs
	if info, ok := HTMLElements[tag]; ok && len(info.Attributes) > 0 {
		attrs = info.Attributes
	}
	kind := protocol.CompletionItemKindProperty
	items := make([]protocol.CompletionItem, 0, len(attrs))
	for _, a := range attrs {
		items = append(items, protocol.CompletionItem{Label: a, Kind: &kind})
	}
	return items
}
