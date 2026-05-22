package lsp

import (
	"fmt"
	"sync"

	minml "github.com/dedis/matchertext/dev/tree-sitter/bindings/go"
	sitter "github.com/tree-sitter/go-tree-sitter"
)

type Document struct {
	URI       string
	Text      string
	TextBytes []byte
	Tree      *sitter.Tree
	Version   int32
}

type Store struct {
	documents map[string]*Document
	// mu guards documents and serialises parser use.
	// sitter.Parser is not goroutine-safe; holding mu for Parse() is intentional.
	mu     sync.RWMutex
	parser *sitter.Parser
}

func NewStore() *Store {
	parser := sitter.NewParser()
	parser.SetLanguage(sitter.NewLanguage(minml.Language()))
	return &Store{
		documents: make(map[string]*Document),
		parser:    parser,
	}
}

func (s *Store) Update(uri string, text string, version int32) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	// Close the old tree before replacing it to free CGo-backed memory.
	if old, ok := s.documents[uri]; ok {
		old.Tree.Close()
	}

	textBytes := []byte(text)
	tree := s.parser.Parse(textBytes, nil)
	if tree == nil {
		return fmt.Errorf("parser returned nil tree for %s", uri)
	}

	s.documents[uri] = &Document{
		URI:       uri,
		Text:      text,
		TextBytes: textBytes,
		Tree:      tree,
		Version:   version,
	}
	return nil
}

// WithDocument calls fn with the named document while holding the read lock,
// preventing the tree from being closed by a concurrent Update while fn runs.
// Returns false if the document is not in the store.
func (s *Store) WithDocument(uri string, fn func(*Document)) bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	doc, ok := s.documents[uri]
	if !ok {
		return false
	}
	fn(doc)
	return true
}

func (d *Document) NodeAt(line, char uint32) *sitter.Node {
	// The LSP cursor sits AFTER the last typed character, which is the exclusive
	// end of the token's range. Step back one column so NamedDescendantForPointRange
	// lands inside the token rather than returning the parent node.
	col := char
	if col > 0 {
		col--
	}
	point := sitter.Point{Row: uint(line), Column: uint(col)}
	return d.Tree.RootNode().NamedDescendantForPointRange(point, point)
}

func (d *Document) lineLength(row uint) uint {
	start := uint(0)
	currentRow := uint(0)
	for i, b := range d.TextBytes {
		if currentRow == row {
			start = uint(i)
			for j := i; j < len(d.TextBytes); j++ {
				if d.TextBytes[j] == '\n' {
					return uint(j) - start
				}
			}
			return uint(len(d.TextBytes)) - start
		}
		if b == '\n' {
			currentRow++
		}
	}
	return 0
}

func (s *Store) Delete(uri string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if doc, ok := s.documents[uri]; ok {
		doc.Tree.Close()
		delete(s.documents, uri)
	}
}

// CloseAll closes all open document trees, freeing CGo-backed memory.
func (s *Store) CloseAll() {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, doc := range s.documents {
		doc.Tree.Close()
	}
	s.documents = make(map[string]*Document)
}
