package xml

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

	// Raw text
	et("<![CDATA[abc]]>", aRawText("abc")),
	et("<![CDATA[&foo;]]>", aRawText("&foo;")),
	et("<![CDATA[<mark></up>]]>", aRawText("<mark></up>")),
	et("<![CDATA[]]]]><![CDATA[>]]>", aRawText("]]>")),
	et("<![CDATA[example <![CDATA[character data]]]]><![CDATA[> section]]>",
		aRawText("example <![CDATA[character data]]> section")),

	// References
	et("&hello;", aRef("hello")),
	et("&#123;", aRef("#123")),
	et("&#xabcd;", aRef("#xabcd")),

	// Elements
	et("<p/>", aElem("p")),
	et("<br/>", aElem("br")),
	et("<em>emphasis</em>", aElem("em", aText("emphasis"))),
	et("<i><b>nested</b></i>",
		aElem("i", aElem("b", aText("nested")))),
	et("<a href=\"foo\">link</a>", aElem("a",
		aAttr("href", aText("foo")),
		aText("link"))),
	et("<img src=\"foo\" alt=\"bar\"/>", aElem("img",
		aAttr("src", aText("foo")),
		aAttr("alt", aText("bar")))),
	et("<x y=\"&amp;&lt;&gt;&quot;'\"/>", aElem("x",
		aAttr("y", aText("&<>\"'")))),
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
