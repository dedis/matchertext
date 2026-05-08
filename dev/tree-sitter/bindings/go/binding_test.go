package tree_sitter_minml_test

import (
	"testing"

	tree_sitter_minml "github.com/dedis/matchertext/dev/tree-sitter/bindings/go"
	tree_sitter "github.com/tree-sitter/go-tree-sitter"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_minml.Language())
	if language == nil {
		t.Errorf("Error loading MinML grammar")
	}
}
