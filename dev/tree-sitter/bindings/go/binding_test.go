package tree_sitter_minml_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_minml "github.com/tree-sitter/tree-sitter-minml/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_minml.Language())
	if language == nil {
		t.Errorf("Error loading MinML grammar")
	}
}
