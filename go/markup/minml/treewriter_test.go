package minml

import (
	"math/rand"
	"strings"
	"testing"

	"github.com/dedis/matchertext/go/markup/ast"
)

type encTest struct {
	ast []ast.Node
	out string
}

func et(out string, ns ...ast.Node) encTest {
	return encTest{ast: ns, out: out}
}

var encTests = []encTest{

	// Document types
	et("![html]", ast.NewDoctype("html")),
	et("![xml]p[]", ast.NewDoctype("xml"), aElem("p")),

	// Simple text
	et(""),
	et("abc", aText("abc")),
	et("abcxyz", aText("abc"), aText("xyz")),
	et("x'\"\r\n\ty", aText("x'\"\r\n\ty")),

	// Simple non-problematic matchertext pairs
	et("()", aText("()")),
	et("[]", aText("[]")),
	et("{}", aText("{}")),
	et("(a)", aText("(a)")),
	et("(x y)", aText("(x y)")),
	et("[x y]", aText("[x y]")),
	et("{a}", aText("{a}")),
	et("{x y}", aText("{x y}")),

	// Raw text
	et("+[abc]", aRawText("abc")),
	et("+[[foo]]", aRawText("[foo]")),
	et("+[mark[up]]", aRawText("mark[up]")),
	et("+[+[nested]]", aRawText("+[nested]")),
	et("+[+[double +[nested]]]", aRawText("+[double +[nested]]")),

	// References
	et("[hello]", aRef("hello")),
	et("[#123]", aRef("#123")),
	et("[#xabcd]", aRef("#xabcd")),

	// Elements
	et("p[]", aElem("p")),
	et("br[]", aElem("br")),
	et("em[emphasis]", aElem("em", aText("emphasis"))),
	et("i[b[nested]]", aElem("i", aElem("b", aText("nested")))),
	et("a{href=[foo]}[link]", aElem("a", aAttr("href", aText("foo")),
		aText("link"))),
	et("img{src=[foo] alt=[bar]}[]", aElem("img",
		aAttr("src", aText("foo")),
		aAttr("alt", aText("bar")))),
	et("x{y=[&<>\"']}[]", aElem("x",
		aAttr("y", aText("&<>\"'")))),

	// False references and elements
	et("[x <]", aText("[x]")),
	et("[x <][y <]", aText("[x][y]")),
	et("a <[]", aText("a[]")),
	et("a <[x y]", aText("a[x y]")),
	et("a <[b <]c", aText("a[b]c")),
	et("a <[b <]c <[d <]", aText("a[b]c[d]")),
	et("a <{}", aText("a{}")),
	et("a <{x y}", aText("a{x y}")),
	et("a <{b}c", aText("a{b}c")),
	et("a <{b}c <{d}", aText("a{b}c{d}")),
	et("a <[lt]", aText("a"), aRef("lt")),
	et("x{y=[a[lt]]}[]", aElem("x", aAttr("y", aText("a"), aRef("lt")))),

	// Unmatched matchers and matcher escapes
	et(`\o()`, aRef(`\o()`)),
	et(`\o()`, aText("(")),
	et(`f\o()x`, aText("f(x")),
	et(`a\c[]b\o{}`, aText("a]b{")),
	et(`\o()\c[]\c()`, aText("(]"), aText(")")),
	et(`p[\o()]`, aElem("p", aText("("))),
	et(`x{y=[\c()]}[]`, aElem("x", aAttr("y", aText(")")))),
	et(`[#92]o()`, aText(`\o()`)),
	et(`[#92]o()`, aText(`\o`), aText("()")),
	et(`a <[#92]c <[x <]`, aText(`a\c[x]`)),
	et(`x{y=[a[#92]c{}]}[]`, aElem("x", aAttr("y", aText(`a\c{}`)))),
	et(`\o\o()`, aText(`\o(`)),
	et(`\o <[lt]`, aText(`\o`), aRef("lt")),
}

func TestTreeWriter(t *testing.T) {
	for i, et := range encTests {
		sb := &strings.Builder{}
		e := NewTreeWriter(sb)
		if err := e.WriteAST(et.ast); err != nil {
			t.Error(err.Error())
		}
		s := sb.String()
		if s != et.out {
			t.Errorf("%v: expected %v output %v", i, et.out, s)
		}
	}
}

// Text written by TreeWriter parses back to the same text.
func TestTreeWriterRoundTrip(t *testing.T) {
	alphabet := []string{"a", " ", "\\", "o", "c", "(", ")", "[", "]", "{", "}"}
	rng := rand.New(rand.NewSource(1))
	for n := 0; n < 20000; n++ {
		var b strings.Builder
		for k := rng.Intn(12); k > 0; k-- {
			b.WriteString(alphabet[rng.Intn(len(alphabet))])
		}
		in := b.String()

		sb := &strings.Builder{}
		if err := NewTreeWriter(sb).WriteAST([]ast.Node{aText(in)}); err != nil {
			t.Fatalf("%q: %v", in, err)
		}
		ns, err := NewTreeParser(strings.NewReader(sb.String())).WithTransformer(EntityTransformer).ParseAST()
		if err != nil {
			t.Fatalf("%q written as %q: %v", in, sb.String(), err)
		}
		var out strings.Builder
		for _, n := range ns {
			out.WriteString(n.(ast.Text).Text())
		}
		if out.String() != in {
			t.Fatalf("%q written as %q reads back as %q", in, sb.String(), out.String())
		}
	}
}

func TestInvalidDoctype(t *testing.T) {
	for _, ns := range [][]ast.Node{
		{ast.NewDoctype("svg")},
		{aText("x"), ast.NewDoctype("html")},
		{aElem("p", ast.NewDoctype("html"))},
	} {
		sb := &strings.Builder{}
		if err := NewTreeWriter(sb).WriteAST(ns); err == nil {
			t.Errorf("%v: expected error, got output %q", ns, sb.String())
		}
	}
}
