package lsp

import (
	"fmt"

	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
	sitter "github.com/tree-sitter/go-tree-sitter"
)

func (s *Server) publishDiagnostics(ctx *glsp.Context, uri string, version int32) {
	var diags []protocol.Diagnostic
	found := s.Store.WithDocument(uri, func(doc *Document) {
		diags = collectDiagnostics(doc.Tree.RootNode(), diags)
	})
	if !found {
		return
	}

	if diags == nil {
		diags = []protocol.Diagnostic{}
	}

	v := uint32(version)
	ctx.Notify(protocol.ServerTextDocumentPublishDiagnostics, protocol.PublishDiagnosticsParams{
		URI:         uri,
		Version:     &v,
		Diagnostics: diags,
	})
}

func collectDiagnostics(node *sitter.Node, diags []protocol.Diagnostic) []protocol.Diagnostic {
	if node.IsError() || node.IsMissing() {
		msg := "Syntax error"
		if node.IsMissing() {
			msg = fmt.Sprintf("Missing %s", node.Kind())
		}

		start := node.StartPosition()
		end := node.EndPosition()

		severity := protocol.DiagnosticSeverityError
		source := serverName
		diag := protocol.Diagnostic{
			Range: protocol.Range{
				Start: protocol.Position{Line: uint32(start.Row), Character: uint32(start.Column)},
				End:   protocol.Position{Line: uint32(end.Row), Character: uint32(end.Column)},
			},
			Severity: &severity,
			Source:   &source,
			Message:  msg,
		}
		diags = append(diags, diag)
		// Do not recurse into error recovery subtrees — their children are
		// reconstructed tokens, not independent syntax errors.
		return diags
	}

	for i := uint(0); i < node.ChildCount(); i++ {
		diags = collectDiagnostics(node.Child(i), diags)
	}

	return diags
}
