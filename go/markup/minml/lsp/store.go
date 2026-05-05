package lsp

import (
	"fmt"
	"sync"

	minml "github.com/dedis/matchertext/dev/tree-sitter/bindings/go"
	sitter "github.com/tree-sitter/go-tree-sitter"
)

type Document struct {
	URI     string
	Text    string
	Tree    *sitter.Tree
	Version int32
}

type Store struct {
	documents map[string]*Document
	mu        sync.RWMutex
	parser    *sitter.Parser
}

func NewStore() *Store {
	parser := sitter.NewParser()
	parser.SetLanguage(sitter.NewLanguage(minml.Language()))
	return &Store{
		documents: make(map[string]*Document),
		parser:    parser,
	}
}

func (s *Store) Update(uri string, text string, version int32) (*Document, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	// Close the old tree before replacing it to free CGo-backed memory.
	if old, ok := s.documents[uri]; ok {
		old.Tree.Close()
	}

	tree := s.parser.Parse([]byte(text), nil)
	if tree == nil {
		return nil, fmt.Errorf("parser returned nil tree for %s", uri)
	}

	doc := &Document{
		URI:     uri,
		Text:    text,
		Tree:    tree,
		Version: version,
	}
	s.documents[uri] = doc
	return doc, nil
}

func (d *Document) NodeAt(line, char uint32) *sitter.Node {
	point := sitter.Point{
		Row:    uint(line),
		Column: uint(char),
	}
	return d.Tree.RootNode().NamedDescendantForPointRange(point, point)
}

func (s *Store) Get(uri string) (*Document, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	doc, ok := s.documents[uri]
	return doc, ok
}

func (s *Store) Delete(uri string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if doc, ok := s.documents[uri]; ok {
		doc.Tree.Close()
		delete(s.documents, uri)
	}
}
