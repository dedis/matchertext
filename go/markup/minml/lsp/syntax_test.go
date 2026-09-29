package lsp

import (
	"errors"
	"math/rand"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/dedis/matchertext/go/markup/minml"
	"github.com/dedis/matchertext/go/matchertext"
	protocol "github.com/tliron/glsp/protocol_3_16"
)

// checkSyntax compares the recovering parse of src with the converter's parser,
// and checks that every mark covers the source text of its construct.
func checkSyntax(t *testing.T, src string) {
	t.Helper()
	d := newDocument(src, 0, nil)
	var problems []Problem
	for _, p := range d.Problems {
		if !p.Hint {
			problems = append(problems, p)
		}
	}

	_, err := minml.NewTreeParser(strings.NewReader(src)).ParseAST()
	var se *matchertext.SyntaxError
	switch {
	case err == nil && len(problems) > 0:
		t.Fatalf("%q: converter accepts, server reports %v", src, problems)
	case err != nil && !errors.As(err, &se):
		t.Fatalf("%q: unexpected error %v", src, err)
	case err != nil && len(problems) == 0:
		t.Fatalf("%q: converter rejects (%v), server reports nothing", src, err)
	case err != nil && int(se.Offset()) != problems[0].Offset:
		t.Fatalf("%q: converter error at %d, server at %d", src, se.Offset(), problems[0].Offset)
	}

	end := 0
	for _, m := range d.Marks {
		if m.Start < end || m.End <= m.Start || m.End > len(src) {
			t.Fatalf("%q: bad mark %+v after offset %d", src, m, end)
		}
		end = m.End
		s := src[m.Start:m.End]
		ok := true
		switch m.Kind {
		case KindTag:
			ok = m.End < len(src) && (src[m.End] == '[' || src[m.End] == '{')
		case KindAttrName:
			ok = src[m.End] == '='
		case KindReference:
			ok = s[0] == '[' && s[len(s)-1] == ']'
		case KindComment:
			ok = strings.HasPrefix(s, "-[")
		case KindRaw:
			ok = strings.HasPrefix(s, "+[")
		}
		if !ok {
			t.Fatalf("%q: mark %+v covers %q", src, m, s)
		}
	}
	d.tokens() // panics if marks are out of order
}

func TestSyntaxCases(t *testing.T) {
	for _, src := range []string{
		"", "p[ok]", "p[see [a b] here]", "p[x (unbalanced]", "p[a {b} c]",
		`p["[a b[em[x]]]]`, "p['[single]]", "p[é[x]]", "p[h1.big[x]]", "p[[&]]",
		"-[comment (open]", "a{href=x.html alt=[a [amp] cat]}[link]", "p{cla", "p{a=b",
		"p{a=b} x", "p{a=b]", "p{(x)=1}[y]", "p{1a=b}[y]", "p{a=[x]y}[z]", "p{a b=c}[d]",
		"]x]", "p[a]]b", "[abc", "[ab(c]", "+[raw <b>]", "+[a (b]", "-[a\nb]\nc[d]",
		"<p[a]> <q[b]>", "p[[[<]] [(>)]]", "x[y{z}]", "p[\r\nq[a]\r\n]",
	} {
		checkSyntax(t, src)
	}
}

func TestSyntaxRandom(t *testing.T) {
	alphabet := []string{"a", "p", "é", "😀", " ", "\n", "[", "]", "{", "}", "(", ")",
		"<", ">", "-", "+", `"`, "'", "=", "#", "&"}
	rng := rand.New(rand.NewSource(1))
	for n := 0; n < 50000; n++ {
		var b strings.Builder
		for k := rng.Intn(24); k > 0; k-- {
			b.WriteString(alphabet[rng.Intn(len(alphabet))])
		}
		checkSyntax(t, b.String())
	}
}

func TestSyntaxCorpus(t *testing.T) {
	files, _ := filepath.Glob("../../../../test/*.m")
	more, _ := filepath.Glob("../../../../test/examples/*.m")
	files = append(files, more...)
	if len(files) == 0 {
		t.Fatal("no corpus files found")
	}
	for _, f := range files {
		src, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		checkSyntax(t, string(src))
	}
}

func TestPositions(t *testing.T) {
	d := newDocument("é😀x\nab", 0, nil)
	for _, c := range []struct {
		off       int
		line, col uint32
	}{{0, 0, 0}, {2, 0, 1}, {6, 0, 3}, {7, 0, 4}, {8, 1, 0}, {10, 1, 2}} {
		p := d.Position(c.off)
		if p.Line != c.line || p.Character != c.col {
			t.Errorf("Position(%d) = %v, want %d:%d", c.off, p, c.line, c.col)
		}
		if o := d.Offset(p); o != c.off {
			t.Errorf("Offset(%v) = %d, want %d", p, o, c.off)
		}
	}
	if o := d.Offset(protocol.Position{Line: 0, Character: 99}); o != 7 {
		t.Errorf("Offset past line end = %d, want 7", o)
	}
}

func TestTokens(t *testing.T) {
	d := newDocument("é[a]\n-[x\ny]", 0, nil)
	want := []uint32{
		0, 0, 1, 0, 0, // é
		1, 0, 3, 4, 0, // -[x
		1, 0, 2, 4, 0, // y]
	}
	if got := d.tokens(); !equal(got, want) {
		t.Errorf("tokens = %v, want %v", got, want)
	}
}

func equal(a, b []uint32) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func TestCompletions(t *testing.T) {
	src := "img{src=a.png al}[] p[te -[co] [amp]"
	d := newDocument(src, 0, nil)
	for _, c := range []struct {
		at        string // cursor goes after the first occurrence of this text
		byTrigger bool
		want      string // a label that must be offered, or "" for none
	}{
		{"img{", true, "src"},
		{"img{src=a", false, ""},
		{"img{src=a.png al", false, "alt"},
		{"p[te", false, "div"},
		{"p[te", true, ""},
		{"-[co", false, ""},
		{"[am", false, ""},
	} {
		items := d.completions(strings.Index(src, c.at)+len(c.at), c.byTrigger)
		found := len(items) > 0
		if c.want != "" {
			found = false
			for _, it := range items {
				found = found || it.Label == c.want
			}
		}
		if found != (c.want != "") {
			t.Errorf("after %q (trigger %v): got %d items, want %q", c.at, c.byTrigger, len(items), c.want)
		}
	}
}

func TestHover(t *testing.T) {
	for _, c := range []struct{ src, want string }{
		{"[amp]", "`&`"},
		{"[[<]]", "`[`"},
		{"[zz]", "literal text"},
		{"[#174]", "`®`"},
		{"-[x]", "HTML comment"},
		{"+[x]", "escaped text"},
		{`"[x]`, "Quotation"},
		{"div[x]", "### `div`"},
	} {
		d := newDocument(c.src, 0, nil)
		i := d.markAt(0)
		if i < 0 {
			t.Fatalf("%q: no mark at 0", c.src)
		}
		m := d.Marks[i]
		if got := hoverText(m.Kind, c.src[m.Start:m.End]); !strings.Contains(got, c.want) {
			t.Errorf("%q: hover %q does not contain %q", c.src, got, c.want)
		}
	}
}

func TestLineEnds(t *testing.T) {
	d := newDocument("-[a\r\nb\rc]", 0, nil)
	for _, c := range []struct {
		off       int
		line, col uint32
	}{{3, 0, 3}, {5, 1, 0}, {7, 2, 0}} {
		if p := d.Position(c.off); p.Line != c.line || p.Character != c.col {
			t.Errorf("Position(%d) = %v, want %d:%d", c.off, p, c.line, c.col)
		}
	}
	if o := d.Offset(protocol.Position{Line: 0, Character: 99}); o != 3 {
		t.Errorf("Offset past line end = %d, want 3", o)
	}
	want := []uint32{
		0, 0, 3, 4, 0, // -[a
		1, 0, 1, 4, 0, // b
		1, 0, 2, 4, 0, // c]
	}
	if got := d.tokens(); !equal(got, want) {
		t.Errorf("tokens = %v, want %v", got, want)
	}
}

func TestReferenceHints(t *testing.T) {
	d := newDocument("[amp] [1] [#174] x[[&]]", 0, nil)
	var hints []string
	for _, p := range d.Problems {
		if !p.Hint {
			t.Fatalf("unexpected error %v", p)
		}
		hints = append(hints, d.Text[p.Offset:p.End])
	}
	if strings.Join(hints, " ") != "[1] [&]" {
		t.Errorf("hints on %q, want [1] and [&]", hints)
	}
}

func TestApplyChanges(t *testing.T) {
	d := newDocument("é[a]\r\nb", 0, nil)
	edit := func(sl, sc, el, ec uint32, text string) protocol.TextDocumentContentChangeEvent {
		return protocol.TextDocumentContentChangeEvent{
			Range: &protocol.Range{
				Start: protocol.Position{Line: sl, Character: sc},
				End:   protocol.Position{Line: el, Character: ec},
			},
			Text: text,
		}
	}
	for _, c := range []struct {
		changes []any
		want    string
	}{
		{[]any{edit(0, 2, 0, 3, "xy")}, "é[xy]\r\nb"},
		{[]any{edit(0, 4, 1, 0, "")}, "é[a]b"},
		{[]any{edit(1, 1, 1, 1, "c"), edit(0, 0, 0, 1, "")}, "[a]\r\nbc"},
		{[]any{edit(0, 0, 0, 1, "p\nq"), edit(1, 0, 1, 1, "Q")}, "p\nQ[a]\r\nb"},
		{[]any{protocol.TextDocumentContentChangeEventWhole{Text: "z"}, edit(0, 1, 0, 1, "!")}, "z!"},
	} {
		if got := applyChanges(d, c.changes, 1).Text; got != c.want {
			t.Errorf("got %q, want %q", got, c.want)
		}
	}
}
