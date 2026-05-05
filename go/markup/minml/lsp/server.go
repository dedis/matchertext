package lsp

import (
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
		Initialize:             s.Initialize,
		Initialized:            s.Initialized,
		Shutdown:               s.Shutdown,
		SetTrace:               s.SetTrace,
		TextDocumentDidOpen:    s.DidOpen,
		TextDocumentDidChange:  s.DidChange,
		TextDocumentDidClose:   s.DidClose,
		TextDocumentCompletion: s.Completion,
		TextDocumentHover:      s.Hover,
	}
	s.Server = server.NewServer(&s.Handler, serverName, debug)
	return s
}

func (s *Server) Initialize(context *glsp.Context, params *protocol.InitializeParams) (any, error) {
	s.Log.Infof("initializing server %s %s", serverName, version)
	capabilities := s.Handler.CreateServerCapabilities()
	syncKind := protocol.TextDocumentSyncKindFull
	capabilities.TextDocumentSync = &syncKind
	capabilities.CompletionProvider = &protocol.CompletionOptions{
		TriggerCharacters: []string{"[", "{"},
	}
	capabilities.HoverProvider = true

	v := version
	return protocol.InitializeResult{
		Capabilities: capabilities,
		ServerInfo: &protocol.InitializeResultServerInfo{
			Name:    serverName,
			Version: &v,
		},
	}, nil
}

func (s *Server) Initialized(context *glsp.Context, params *protocol.InitializedParams) error {
	s.Log.Info("server initialized")
	return nil
}

func (s *Server) Shutdown(context *glsp.Context) error {
	s.Log.Info("server shutting down")
	return nil
}

func (s *Server) SetTrace(context *glsp.Context, params *protocol.SetTraceParams) error {
	s.Log.Infof("set trace: %s", params.Value)
	return nil
}

func (s *Server) DidOpen(context *glsp.Context, params *protocol.DidOpenTextDocumentParams) error {
	s.Log.Debugf("didOpen: %s", params.TextDocument.URI)
	doc, err := s.Store.Update(params.TextDocument.URI, params.TextDocument.Text, params.TextDocument.Version)
	if err != nil {
		s.Log.Errorf("failed to parse document on open: %v", err)
		return nil
	}
	s.publishDiagnostics(context, doc)
	return nil
}

func (s *Server) DidChange(context *glsp.Context, params *protocol.DidChangeTextDocumentParams) error {
	s.Log.Debugf("didChange: %s", params.TextDocument.URI)
	if len(params.ContentChanges) == 0 {
		return nil
	}
	change, ok := params.ContentChanges[0].(protocol.TextDocumentContentChangeEventWhole)
	if !ok {
		s.Log.Warning("received incremental content change despite advertising full sync — document may be stale")
		return nil
	}
	doc, err := s.Store.Update(params.TextDocument.URI, change.Text, params.TextDocument.Version)
	if err != nil {
		s.Log.Errorf("failed to parse document on change: %v", err)
		return nil
	}
	s.publishDiagnostics(context, doc)
	return nil
}

func (s *Server) DidClose(context *glsp.Context, params *protocol.DidCloseTextDocumentParams) error {
	s.Log.Debugf("didClose: %s", params.TextDocument.URI)
	s.Store.Delete(params.TextDocument.URI)
	return nil
}
