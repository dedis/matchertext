package lsp

import (
	_ "embed"
	"sort"

	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
	sitter "github.com/tree-sitter/go-tree-sitter"
)

//go:embed queries/highlights.scm
var highlightsQuery string

var tokenTypes = []string{
	"type",     // @tag
	"property", // @tag.attribute
	"operator", // @punctuation.bracket, @punctuation.delimiter
	"constant", // @constant.builtin
	"string",   // @string, @string.special, @string.escape
	"comment",  // @comment
	"keyword",  // @keyword.directive
}

var tokenModifiers = []string{
	"defaultLibrary",
}

func (s *Server) SemanticTokensFull(_ *glsp.Context, params *protocol.SemanticTokensParams) (*protocol.SemanticTokens, error) {
	var tokens []uint32

	s.Store.WithDocument(params.TextDocument.URI, func(doc *Document) {
		query, err := sitter.NewQuery(doc.Tree.Language(), highlightsQuery)
		if err != nil {
			s.Log.Errorf("failed to create query: %v", err)
			return
		}
		defer query.Close()

		cursor := sitter.NewQueryCursor()
		defer cursor.Close()

		captures := cursor.Captures(query, doc.Tree.RootNode(), doc.TextBytes)

		type rawToken struct {
			line, col, length uint32
			tokenType         uint32
		}
		var rawTokens []rawToken

		captureNames := query.CaptureNames()
		for {
			match, captureIndex := captures.Next()
			if match == nil {
				break
			}
			capture := match.Captures[captureIndex]
			name := captureNames[capture.Index]

			var tokenType uint32
			found := true
			switch name {
			case "tag":
				tokenType = 0 // type
			case "tag.attribute":
				tokenType = 1 // property
			case "punctuation.bracket", "punctuation.delimiter":
				tokenType = 2 // operator
			case "constant.builtin":
				tokenType = 3 // constant
			case "string", "string.special", "string.escape":
				tokenType = 4 // string
			case "comment":
				tokenType = 5 // comment
			case "keyword.directive":
				tokenType = 6 // keyword
			default:
				found = false
			}

			if found {
				start := capture.Node.StartPosition()
				end := capture.Node.EndPosition()

				if start.Row == end.Row {
					rawTokens = append(rawTokens, rawToken{
						line:      uint32(start.Row),
						col:       uint32(start.Column),
						length:    uint32(end.Column - start.Column),
						tokenType: tokenType,
					})
				} else {
					// Split multi-line token
					for row := start.Row; row <= end.Row; row++ {
						var col uint32
						var length uint32
						if row == start.Row {
							col = uint32(start.Column)
							length = uint32(doc.lineLength(row) - start.Column)
						} else if row == end.Row {
							col = 0
							length = uint32(end.Column)
						} else {
							col = 0
							length = uint32(doc.lineLength(row))
						}
						if length > 0 {
							rawTokens = append(rawTokens, rawToken{
								line:      uint32(row),
								col:       col,
								length:    length,
								tokenType: tokenType,
							})
						}
					}
				}
			}
		}

		sort.Slice(rawTokens, func(i, j int) bool {
			if rawTokens[i].line != rawTokens[j].line {
				return rawTokens[i].line < rawTokens[j].line
			}
			return rawTokens[i].col < rawTokens[j].col
		})

		var lastLine, lastCol uint32
		for _, t := range rawTokens {
			deltaLine := t.line - lastLine
			deltaCol := t.col
			if deltaLine == 0 {
				deltaCol = t.col - lastCol
			}
			tokens = append(tokens, deltaLine, deltaCol, t.length, t.tokenType, 0)
			lastLine = t.line
			lastCol = t.col
		}
	})

	return &protocol.SemanticTokens{
		Data: tokens,
	}, nil
}
