package minml

import (
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
