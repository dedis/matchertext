// parser.go
// Go counterpart of Parser::ParseC_CPP.
// Emits a JSON array of {"kind": "string"|"comment", "value": "..."} on stdout.
// Usage: go run parser.go <path>
package main

import (
	"encoding/json"
	"go/scanner"
	"go/token"
	"os"
	"strings"
)

type Item struct {
	Kind  string `json:"kind"`
	Value string `json:"value"`
}

// extractStringBody strips the surrounding delimiters of a Go string literal.
// Supports interpreted strings ("...") and raw strings (`...`); preserves
// inner content verbatim — escape sequences are kept as written.
func extractStringBody(lit string) string {
	if len(lit) < 2 {
		return lit
	}
	first, last := lit[0], lit[len(lit)-1]
	if (first == '"' && last == '"') || (first == '`' && last == '`') {
		return lit[1 : len(lit)-1]
	}
	return lit
}

func parse(path string) []Item {
	items := make([]Item, 0)

	src, err := os.ReadFile(path)
	if err != nil {
		return items
	}

	fset := token.NewFileSet()
	file := fset.AddFile(path, fset.Base(), len(src))
	var s scanner.Scanner
	s.Init(file, src, func(_ token.Position, _ string) {}, scanner.ScanComments)

	var pending strings.Builder
	hasPending := false
	flushPending := func() {
		if hasPending {
			items = append(items, Item{Kind: "string", Value: pending.String()})
			pending.Reset()
			hasPending = false
		}
	}

	for {
		_, tok, lit := s.Scan()
		if tok == token.EOF {
			break
		}
		switch tok {
		case token.STRING:
			pending.WriteString(extractStringBody(lit))
			hasPending = true
		case token.COMMENT:
			flushPending()
			items = append(items, Item{Kind: "comment", Value: lit})
		case token.SEMICOLON:
			// Inserted by the scanner — does not break adjacency for our purposes.
		default:
			flushPending()
		}
	}
	flushPending()

	return items
}

func main() {
	if len(os.Args) < 2 {
		os.Stdout.WriteString("[]")
		return
	}
	items := parse(os.Args[1])
	enc := json.NewEncoder(os.Stdout)
	enc.SetEscapeHTML(false)
	_ = enc.Encode(items)
}
