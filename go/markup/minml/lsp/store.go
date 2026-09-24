package lsp

import (
	"sort"
	"sync"
	"unicode/utf8"

	protocol "github.com/tliron/glsp/protocol_3_16"
)

// Document is an immutable parsed version of an open file.
type Document struct {
	Text    string
	Version int32
	lines   []int // byte offset of the start of each line; LSP ends lines at "\n", "\r\n", or "\r"
	Syntax
}

// newDocument parses text. prev is the previous version of the same file, or nil.
func newDocument(text string, version int32, prev *Document) *Document {
	marks := 0
	if prev != nil {
		marks = len(prev.Marks) + len(prev.Marks)/8 // room for the edit
	}
	return &Document{Text: text, Version: version, lines: lineStarts(text), Syntax: parseSyntax(text, marks)}
}

func lineStarts(text string) []int {
	lines := []int{0}
	for i := 0; i < len(text); i++ {
		if isLineEnd(text, i) {
			lines = append(lines, i+1)
		}
	}
	return lines
}

// applyChanges returns d after the didChange content changes, in order.
// A ranged change reparses only around the edit; a change without a range replaces the whole text.
func applyChanges(d *Document, changes []any, version int32) *Document {
	for _, c := range changes {
		switch c := c.(type) {
		case protocol.TextDocumentContentChangeEventWhole:
			d = newDocument(c.Text, version, d)
		case protocol.TextDocumentContentChangeEvent:
			d = d.edit(d.Offset(c.Range.Start), d.Offset(c.Range.End), c.Text, version)
		}
	}
	return d
}

// edit returns d with the bytes [s, e) replaced by ins.
func (d *Document) edit(s, e int, ins string, version int32) *Document {
	text := d.Text[:s] + ins + d.Text[e:]
	return &Document{
		Text:    text,
		Version: version,
		lines:   spliceLines(d.lines, text, s, e, len(ins)),
		Syntax:  reparse(&d.Syntax, text, s, e, len(ins)),
	}
}

// isLineEnd reports whether text[i] is the last byte of a line terminator.
func isLineEnd(text string, i int) bool {
	return text[i] == '\n' || text[i] == '\r' && (i+1 == len(text) || text[i+1] != '\n')
}

// lineEnd returns the offset of the terminator of line l, or the end of the text.
func (d *Document) lineEnd(l int) int {
	if l+1 == len(d.lines) {
		return len(d.Text)
	}
	end := d.lines[l+1] - 1
	if end > d.lines[l] && d.Text[end] == '\n' && d.Text[end-1] == '\r' {
		end--
	}
	return end
}

// utf16Units returns the length of r in UTF-16 code units, the LSP default position encoding.
func utf16Units(r rune) uint32 {
	if r >= 0x10000 {
		return 2
	}
	return 1
}

func utf16Len(s string) uint32 {
	n := uint32(0)
	for _, r := range s {
		n += utf16Units(r)
	}
	return n
}

// Position converts byte offset o to an LSP position.
func (d *Document) Position(o int) protocol.Position {
	line := sort.SearchInts(d.lines, o+1) - 1
	return protocol.Position{Line: uint32(line), Character: utf16Len(d.Text[d.lines[line]:o])}
}

// Range converts the byte range [start, end) to an LSP range.
func (d *Document) Range(start, end int) protocol.Range {
	return protocol.Range{Start: d.Position(start), End: d.Position(end)}
}

// Offset converts an LSP position to a byte offset, clamped to the line and the document.
func (d *Document) Offset(p protocol.Position) int {
	if int(p.Line) >= len(d.lines) {
		return len(d.Text)
	}
	o := d.lines[p.Line]
	end := d.lineEnd(int(p.Line))
	for u := uint32(0); o < end && u < p.Character; {
		r, n := utf8.DecodeRuneInString(d.Text[o:])
		u += utf16Units(r)
		o += n
	}
	return o
}

// markAt returns the index of the mark containing byte offset o, or -1.
func (d *Document) markAt(o int) int {
	i := sort.Search(len(d.Marks), func(i int) bool { return d.Marks[i].End > o })
	if i < len(d.Marks) && d.Marks[i].Start <= o {
		return i
	}
	return -1
}

// Store holds the open documents.
// The TCP transport serves several connections at once, so access is locked.
type Store struct {
	mu        sync.RWMutex
	documents map[string]*Document
}

func NewStore() *Store {
	return &Store{documents: make(map[string]*Document)}
}

// Update parses text and stores it as the current version of uri.
func (s *Store) Update(uri string, text string, version int32) *Document {
	d := newDocument(text, version, s.Get(uri))
	s.mu.Lock()
	s.documents[uri] = d
	s.mu.Unlock()
	return d
}

// Set stores d as the current version of uri.
func (s *Store) Set(uri string, d *Document) {
	s.mu.Lock()
	s.documents[uri] = d
	s.mu.Unlock()
}

// Get returns the current version of uri, or nil if it is not open.
func (s *Store) Get(uri string) *Document {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.documents[uri]
}

func (s *Store) Delete(uri string) {
	s.mu.Lock()
	delete(s.documents, uri)
	s.mu.Unlock()
}

// CloseAll forgets all open documents.
func (s *Store) CloseAll() {
	s.mu.Lock()
	s.documents = make(map[string]*Document)
	s.mu.Unlock()
}
