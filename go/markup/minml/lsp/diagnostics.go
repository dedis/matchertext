package lsp

import (
	"unicode/utf8"

	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
)

func (s *Server) publishDiagnostics(ctx *glsp.Context, uri string, d *Document) {
	v := uint32(d.Version)
	ctx.Notify(protocol.ServerTextDocumentPublishDiagnostics, protocol.PublishDiagnosticsParams{
		URI:         uri,
		Version:     &v,
		Diagnostics: d.diagnostics(),
	})
}

// diagnostics returns one diagnostic per problem.
// A syntax error covers the character at the error.
func (d *Document) diagnostics() []protocol.Diagnostic {
	diags := make([]protocol.Diagnostic, 0, len(d.Problems))
	errSeverity := protocol.DiagnosticSeverityError
	hintSeverity := protocol.DiagnosticSeverityHint
	source := serverName
	for _, p := range d.Problems {
		severity, end := &hintSeverity, p.End
		if !p.Hint {
			_, n := utf8.DecodeRuneInString(d.Text[p.Offset:])
			severity, end = &errSeverity, p.Offset+n
		}
		diags = append(diags, protocol.Diagnostic{
			Range:    d.Range(p.Offset, end),
			Severity: severity,
			Source:   &source,
			Message:  p.Message,
		})
	}
	return diags
}
