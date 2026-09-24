package lsp

import (
	"unicode/utf8"

	"github.com/tliron/glsp"
	protocol "github.com/tliron/glsp/protocol_3_16"
)

var tokenTypes = []string{
	"type",     // element name
	"property", // attribute name
	"operator", // attribute and content brackets
	"constant", // character reference
	"string",   // raw text, quotation element name
	"comment",  // comment
}

var tokenModifiers = []string{}

func tokenType(k Kind, src string) uint32 {
	switch k {
	case KindTag:
		if src == `"` || src == "'" {
			return 4
		}
		return 0
	case KindAttrName:
		return 1
	case KindBracket:
		return 2
	case KindReference:
		return 3
	case KindRaw:
		return 4
	}
	return 5 // KindComment
}

func (s *Server) SemanticTokensFull(_ *glsp.Context, params *protocol.SemanticTokensParams) (*protocol.SemanticTokens, error) {
	d := s.Store.Get(params.TextDocument.URI)
	if d == nil {
		return &protocol.SemanticTokens{Data: []uint32{}}, nil
	}
	return &protocol.SemanticTokens{Data: d.tokens()}, nil
}

// tokens encodes the marks as LSP semantic tokens, split at line ends.
// A single forward scan converts offsets to UTF-16 positions, so the cost is linear in the text.
func (d *Document) tokens() []uint32 {
	data := make([]uint32, 0, 5*len(d.Marks))
	var o int                    // scan offset
	var line, col uint32         // position of o
	var lastLine, lastCol uint32 // position of the previous token
	advance := func(to int) {
		if to < o {
			panic("lsp: marks out of document order")
		}
		for o < to {
			if d.Text[o] == '\r' || d.Text[o] == '\n' {
				if isLineEnd(d.Text, o) {
					line++
					col = 0
				}
				o++
				continue
			}
			r, n := utf8.DecodeRuneInString(d.Text[o:])
			col += utf16Units(r)
			o += n
		}
	}
	for _, m := range d.Marks {
		typ := tokenType(m.Kind, d.Text[m.Start:m.End])
		for start := m.Start; start < m.End; {
			advance(start)
			startLine, startCol := line, col
			end := min(m.End, d.lineEnd(int(line)))
			advance(end)
			if col > startCol {
				deltaCol := startCol
				if startLine == lastLine {
					deltaCol -= lastCol
				}
				data = append(data, startLine-lastLine, deltaCol, col-startCol, typ, 0)
				lastLine, lastCol = startLine, startCol
			}
			start = m.End // the mark ends on this line, unless another line follows
			if int(line)+1 < len(d.lines) {
				start = min(start, d.lines[line+1])
			}
		}
	}
	return data
}
