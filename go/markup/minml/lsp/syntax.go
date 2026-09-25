package lsp

import (
	"errors"
	"strings"

	"github.com/dedis/matchertext/go/markup/minml"
	"github.com/dedis/matchertext/go/matchertext"
)

// Kind classifies a Mark.
type Kind uint8

const (
	KindTag       Kind = iota // element name
	KindAttrName              // attribute name
	KindReference             // character reference [name]
	KindComment               // comment -[...]
	KindRaw                   // raw text +[...]
)

// Mark is a highlighted source range [Start, End) in bytes.
// Syntax.Marks is in document order and marks do not overlap.
type Mark struct {
	Start, End int
	Kind       Kind
}

// AttrBlock is the {...} attribute block of an element.
type AttrBlock struct {
	Tag string
	// Offset of '{', and offset of '}' or of where error recovery closed the block.
	Open, Close int
	// Values holds, per attribute, the offset of '=' and the offset where the value ends.
	Values [][2]int
}

// Problem is a syntax error at byte offset Offset,
// or a hint about the construct at [Offset, End).
type Problem struct {
	Offset  int
	End     int // 0 for a syntax error, which covers the character at Offset
	Message string
	Hint    bool
}

// Sync is a restart point: an element that starts directly in its parent's content,
// or at top level, outside any other matcher pair.
// A fresh parser started at Start reports the same constructs as the full parse from there on.
type Sync struct {
	Start    int
	Parent   int // index in Syncs of the parent element, or -1 at top level
	Problems int // number of problems reported before this element
}

// Syntax is what the language server needs from one parse of a document.
type Syntax struct {
	Marks    []Mark
	Blocks   []AttrBlock // in document order; blocks never nest
	Problems []Problem   // in the order the parser reported them, which is not document order
	Syncs    []Sync      // in document order
}

// Parse returns the syntax of src.
// The tree-sitter grammar's tests compare their trees against it.
func Parse(src string) Syntax {
	return parseSyntax(src, 0)
}

// parseSyntax parses src with the reference MinML parser and records
// the source ranges of its constructs. Syntax errors do not stop the parse.
// marks is the expected number of marks, to size the mark slice once.
func parseSyntax(src string, marks int) Syntax {
	r := newRecorder(src, 0, -1, 0, 0)
	r.syn.Marks = make([]Mark, 0, marks)
	if err := r.p.ReadAll(r); err != nil {
		// Only syntax errors can occur on a string, and the handler recovers from all of them.
		panic(err)
	}
	return r.syn
}

// recorder implements the MinML parser's handler interfaces.
// Each callback derives source offsets from the parser's position:
// an element is reported with its opener as the last byte read,
// an attribute with its '=', and a reference, comment, or raw text with its closer.
type recorder struct {
	p        minml.Parser
	src      string
	base     int // offset in src where the parser's input starts
	syn      Syntax
	block    int   // index in syn.Blocks of the attribute block being parsed, or -1
	elem     int   // global sync index of the element being parsed, or noSync
	parents  []int // global sync index of each enclosing element content, innermost last
	syncBase int   // global index of syn.Syncs[0]
	probBase int   // global index of syn.Problems[0]

	// stop, if set, is called at each sync before it is recorded;
	// returning true ends the parse with errStop.
	stop func(start, parent int) bool
}

var errStop = errors.New("lsp: parse stopped at a restart point")

// noSync marks an element that is not a sync; its descendants are not syncs either.
const noSync = -2

// newRecorder returns a recorder that parses src from offset base.
// parent is the global sync index of the element whose content contains base, or -1;
// syncBase and probBase are the global indexes of the first recorded sync and problem.
func newRecorder(src string, base, parent, syncBase, probBase int) *recorder {
	r := &recorder{src: src, base: base, block: -1, elem: noSync, parents: []int{parent}, syncBase: syncBase, probBase: probBase}
	r.p.SetReader(strings.NewReader(src[base:]))
	r.p.SetErrorHandler(func(err error) error {
		e := err.(*matchertext.SyntaxError)
		r.syn.Problems = append(r.syn.Problems, Problem{Offset: base + int(e.Offset()), Message: e.Message()})
		return nil
	})
	return r
}

func (r *recorder) offset() int {
	return r.base + int(r.p.Offset())
}

// mark records a construct; error recovery can report an empty attribute name, which gets no mark.
func (r *recorder) mark(k Kind, start, end int) {
	if start < end {
		r.syn.Marks = append(r.syn.Marks, Mark{start, end, k})
	}
}

func (r *recorder) Element(name []byte) error {
	open := r.offset()
	start := open - len(name)

	// Only the parent's content pair, and those of its ancestors, may enclose a sync.
	// A fresh parser would drop a leading '<' as a space sucker, so such a name is no sync.
	outerElem := r.elem
	r.elem = noSync
	parent := r.parents[len(r.parents)-1]
	if parent != noSync && r.p.Depth() == len(r.parents)-1 && name[0] != '<' {
		if r.stop != nil && r.stop(start, parent) {
			return errStop
		}
		r.elem = r.syncBase + len(r.syn.Syncs)
		r.syn.Syncs = append(r.syn.Syncs, Sync{start, parent, r.probBase + len(r.syn.Problems)})
	}
	r.mark(KindTag, start, open)

	outer := r.block
	r.block = -1
	if r.src[open] == '{' {
		r.block = len(r.syn.Blocks)
		r.syn.Blocks = append(r.syn.Blocks, AttrBlock{Tag: string(name), Open: open})
	}
	e := r.p.ReadElement(name, r)
	r.block = outer
	r.elem = outerElem
	return e
}

func (r *recorder) Attribute(name []byte) error {
	eq := r.offset()
	r.mark(KindAttrName, eq-len(name), eq)
	if e := r.p.ReadAttribute(name, r); e != nil {
		return e
	}
	b := &r.syn.Blocks[r.block]
	b.Values = append(b.Values, [2]int{eq, r.offset()})
	return nil
}

func (r *recorder) Content() error {
	if r.block >= 0 {
		// The last byte read is the block's '}', or where error recovery closed the block.
		r.syn.Blocks[r.block].Close = r.offset()
		r.block = -1
	}
	r.parents = append(r.parents, r.elem)
	e := r.p.ReadContent(r)
	r.parents = r.parents[:len(r.parents)-1]
	return e
}

func (r *recorder) Text(text []byte, raw bool) error {
	if raw {
		r.verbatim(KindRaw, len(text))
	}
	return nil
}

func (r *recorder) Comment(text []byte) error {
	r.verbatim(KindComment, len(text))
	return nil
}

// verbatim marks a two-byte opener (+[ or -[), n content bytes, and the closer if present.
func (r *recorder) verbatim(k Kind, n int) {
	end := r.offset()
	start := end - n - 2
	if !r.p.Unclosed() {
		end++
	}
	r.mark(k, start, end)
}

func (r *recorder) Reference(name []byte) error {
	end := r.offset() + 1
	start := end - len(name) - 2
	r.mark(KindReference, start, end)
	if _, ok := minml.LookupReference(string(name)); !ok {
		r.syn.Problems = append(r.syn.Problems, Problem{start, end,
			"Not a character reference; converted to the literal text " + r.src[start:end], true})
	}
	return nil
}
