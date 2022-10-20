package html

import (
	"strings"
	"testing"

	"github.com/dedis/matchertext/go/xml/ast"
)

type encTest struct {
	ast []ast.Node
	out string
}

func aText(s string, raw bool) ast.Text {
	return ast.NewText(s, raw)
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
	et("abc", aText("abc", false)),
	et("abcxyz", aText("abc", false), aText("xyz", false)),
	et("x'\"\r\n\ty", aText("x'\"\r\n\ty", false)),
	et("a&lt;b&gt;c&amp;d'e\"f", aText("a<b>c&d'e\"f", false)),

	// Raw text - output normally because HTML has no CDATA sections
	et("abc", aText("abc", true)),
	et("&amp;foo;", aText("&foo;", true)),
	et("&lt;mark&gt;&lt;/up&gt;", aText("<mark></up>", true)),
	et("]]&gt;", aText("]]>", true)),

	// References
	et("&hello;", aRef("hello")),
	et("&#123;", aRef("#123")),
	et("&#xabcd;", aRef("#xabcd")),

	// Elements
	et("<p></p>", aElem("p")),
	et("<em>emphasis</em>", aElem("em", aText("emphasis", false))),
	et("<i><b>nested</b></i>",
		aElem("i", aElem("b", aText("nested", false)))),
	et("<hr width=\"100%\"/>",
		aElem("hr", aAttr("width", aText("100%", false)))),
	et("<a href=\"foo\">link</a>", aElem("a",
		aAttr("href", aText("foo", false)),
		aText("link", false))),
	et("<img src=\"foo\" alt=\"bar\"/>", aElem("img",
		aAttr("src", aText("foo", false)),
		aAttr("alt", aText("bar", false)))),
	et("<x y=\"&amp;&lt;&gt;&quot;'\"></x>", aElem("x",
		aAttr("y", aText("&<>\"'", false)))),
}

func TestEncoder(t *testing.T) {
	for i, et := range encTests {
		sb := &strings.Builder{}
		e := NewEncoder(sb)
		if err := e.Encode(et.ast); err != nil {
			t.Error(err.Error())
		}
		s := sb.String()
		if s != et.out {
			t.Errorf("%v: expected %v output %v", i, et.out, s)
		}
	}
}
