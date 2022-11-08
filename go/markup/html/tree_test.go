package html

import (
	"strings"
	"testing"

	"github.com/dedis/matchertext/go/markup/ast"
)

type encTest struct {
	ast []ast.Node
	out string
}

func aText(s string) ast.Text {
	return ast.NewText(s)
}

func aRawText(s string) ast.Text {
	return ast.NewRawText(s)
}

func aComment(s string) ast.Comment {
	return ast.NewComment(s)
}

func aRef(name string) ast.Reference {
	return ast.NewReference(name)
}

func aAttr(name string, ns ...ast.Node) ast.Attribute {
	return ast.NewAttribute(name, ns...)
}

func aElem(name string, ns ...ast.Node) ast.Element {
	return ast.NewElement(name, ns...)
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
	et("a&lt;b&gt;c&amp;d'e\"f", aText("a<b>c&d'e\"f")),

	// Raw text - output normally because HTML has no CDATA sections
	et("abc", aRawText("abc")),
	et("&amp;foo;", aRawText("&foo;")),
	et("&lt;mark&gt;&lt;/up&gt;", aRawText("<mark></up>")),
	et("]]&gt;", aRawText("]]>")),

	// References
	et("&hello;", aRef("hello")),
	et("&#123;", aRef("#123")),
	et("&#xabcd;", aRef("#xabcd")),

	// Elements
	et("<p></p>", aElem("p")),
	et("<em>emphasis</em>", aElem("em", aText("emphasis"))),
	et("<i><b>nested</b></i>",
		aElem("i", aElem("b", aText("nested")))),
	et("<hr width=\"100%\"/>",
		aElem("hr", aAttr("width", aText("100%")))),
	et("<a href=\"foo\">link</a>", aElem("a",
		aAttr("href", aText("foo")),
		aText("link"))),
	et("<img src=\"foo\" alt=\"bar\"/>", aElem("img",
		aAttr("src", aText("foo")),
		aAttr("alt", aText("bar")))),
	et("<x y=\"&amp;&lt;&gt;&quot;'\"></x>", aElem("x",
		aAttr("y", aText("&<>\"'")))),
}

func TestEncoder(t *testing.T) {
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
