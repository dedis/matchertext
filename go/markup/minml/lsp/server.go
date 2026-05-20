package lsp

import (
	"fmt"

	"github.com/tliron/commonlog"
	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
	"github.com/tliron/glsp/server"
)

const (
	version    = "0.0.1"
	serverName = "minml-lsp"
)

type Server struct {
	Handler protocol.Handler
	Server  *server.Server
	Store   *Store
	Log     commonlog.Logger
}

func NewServer(debug bool) *Server {
	s := &Server{
		Store: NewStore(),
		Log:   commonlog.GetLogger(serverName),
	}
	s.Handler = protocol.Handler{
		Initialize:                     s.Initialize,
		Initialized:                    s.Initialized,
		Shutdown:                       s.Shutdown,
		SetTrace:                       s.SetTrace,
		TextDocumentDidOpen:            s.DidOpen,
		TextDocumentDidChange:          s.DidChange,
		TextDocumentDidClose:           s.DidClose,
		TextDocumentCompletion:         s.Completion,
		TextDocumentHover:              s.Hover,
		TextDocumentSemanticTokensFull: s.SemanticTokensFull,
	}
	s.Server = server.NewServer(&s.Handler, serverName, debug)
	return s
}

func (s *Server) Initialize(_ *glsp.Context, params *protocol.InitializeParams) (any, error) {
	s.Log.Infof("initializing server %s %s", serverName, version)
	capabilities := s.Handler.CreateServerCapabilities()
	syncKind := protocol.TextDocumentSyncKindFull
	capabilities.TextDocumentSync = &syncKind
	capabilities.CompletionProvider = &protocol.CompletionOptions{
		// { triggers attribute completions; tag completions fire on identifier characters
		TriggerCharacters: []string{"{"},
	}
	capabilities.HoverProvider = true
	capabilities.SemanticTokensProvider = protocol.SemanticTokensRegistrationOptions{
		SemanticTokensOptions: protocol.SemanticTokensOptions{
			Legend: protocol.SemanticTokensLegend{
				TokenTypes:     tokenTypes,
				TokenModifiers: tokenModifiers,
			},
			Full: true,
		},
	}

	v := version
	return protocol.InitializeResult{
		Capabilities: capabilities,
		ServerInfo: &protocol.InitializeResultServerInfo{
			Name:    serverName,
			Version: &v,
		},
	}, nil
}

func (s *Server) Initialized(_ *glsp.Context, _ *protocol.InitializedParams) error {
	s.Log.Info("server initialized")
	return nil
}

func (s *Server) Shutdown(_ *glsp.Context) error {
	s.Log.Info("server shutting down")
	s.Store.CloseAll()
	return nil
}

func (s *Server) SetTrace(_ *glsp.Context, params *protocol.SetTraceParams) error {
	s.Log.Infof("set trace: %s", params.Value)
	return nil
}

func (s *Server) DidOpen(context *glsp.Context, params *protocol.DidOpenTextDocumentParams) error {
	s.Log.Debugf("didOpen: %s", params.TextDocument.URI)
	if err := s.Store.Update(params.TextDocument.URI, params.TextDocument.Text, params.TextDocument.Version); err != nil {
		s.Log.Errorf("failed to parse document on open: %v", err)
		return nil
	}
	s.publishDiagnostics(context, params.TextDocument.URI, params.TextDocument.Version)
	return nil
}

func (s *Server) DidChange(context *glsp.Context, params *protocol.DidChangeTextDocumentParams) error {
	s.Log.Debugf("didChange: %s", params.TextDocument.URI)
	if len(params.ContentChanges) == 0 {
		return nil
	}

	// We expect a single full-document sync change.
	change, ok := params.ContentChanges[0].(protocol.TextDocumentContentChangeEventWhole)
	if !ok {
		// Receiving incremental changes when we advertised full sync is a client-side error.
		err := fmt.Errorf("minml-lsp requires full document sync (TextDocumentSyncKindFull)")
		s.Log.Error(err.Error())
		return err
	}

	if err := s.Store.Update(params.TextDocument.URI, change.Text, params.TextDocument.Version); err != nil {
		s.Log.Errorf("failed to parse document on change: %v", err)
		return err
	}
	s.publishDiagnostics(context, params.TextDocument.URI, params.TextDocument.Version)
	return nil
}

func (s *Server) DidClose(_ *glsp.Context, params *protocol.DidCloseTextDocumentParams) error {
	s.Log.Debugf("didClose: %s", params.TextDocument.URI)
	s.Store.Delete(params.TextDocument.URI)
	return nil
}
