package lsp

import (
	"fmt"

	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
	sitter "github.com/tree-sitter/go-tree-sitter"
)

func (s *Server) publishDiagnostics(context *glsp.Context, doc *Document) {
	diagnostics := []protocol.Diagnostic{}

	// Walk the tree to find ERROR and MISSING nodes
	root := doc.Tree.RootNode()
	diagnostics = s.collectDiagnostics(root, diagnostics)

	version := uint32(doc.Version)
	params := protocol.PublishDiagnosticsParams{
		URI:         doc.URI,
		Version:     &version,
		Diagnostics: diagnostics,
	}
	context.Notify(protocol.ServerTextDocumentPublishDiagnostics, params)
}

func (s *Server) collectDiagnostics(node *sitter.Node, diags []protocol.Diagnostic) []protocol.Diagnostic {
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
		diags = s.collectDiagnostics(node.Child(i), diags)
	}

	return diags
}
