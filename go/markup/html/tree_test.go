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

	// Document type
	et("<!DOCTYPE html>", ast.NewDoctype("html")),

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

	// Raw text elements: script and style content is not escaped
	et("<script>if (a < b && c > 0) {}</script>", aElem("script", aRawText("if (a < b && c > 0) {}"))),
	et("<style>a > b {}</style>", aElem("style", aText("a > b {}"))),
	et("<SCRIPT>a&b</SCRIPT>", aElem("SCRIPT", aText("a"), aText("&b"))),
	et("<script>a</script</script>", aElem("script", aText("a</script"))),
	et("<script></scripts></script>", aElem("script", aText("</scripts>"))),
	et("<style></script></style>", aElem("style", aText("</script>"))),
	et("<script><!-- x --></script>", aElem("script", aText("<!-- x -->"))),
	et("<script><!--<script>x</script>--></script>", aElem("script", aText("<!--<script>x</script>-->"))),
	et("<script><!--<script>--></script>", aElem("script", aText("<!--<script>-->"))),
	et("<script><!--<script>-</script></script>", aElem("script", aText("<!--<script>-</script>"))),
	et("<script><!--><script></script>", aElem("script", aText("<!--><script>"))),
	et("<script><!--<scripts></script>", aElem("script", aText("<!--<scripts>"))),
	et("<script><script></script>", aElem("script", aText("<script>"))),

	// SVG and MathML content is escaped, except in elements that hold HTML
	et("<svg><style>a &gt; b</style></svg>", aElem("svg", aElem("style", aText("a > b")))),
	et("<svg><g><script>a &lt; b</script></g></svg>",
		aElem("svg", aElem("g", aElem("script", aText("a < b"))))),
	et("<svg><foreignObject><style>a > b</style></foreignObject></svg>",
		aElem("svg", aElem("foreignObject", aElem("style", aText("a > b"))))),
	et("<svg><title><style>a > b</style></title></svg>",
		aElem("svg", aElem("title", aElem("style", aText("a > b"))))),
	et("<math><mi><script>a < b</script></mi></math>",
		aElem("math", aElem("mi", aElem("script", aText("a < b"))))),
	et("<math><mrow><style>a &gt; b</style></mrow></math>",
		aElem("math", aElem("mrow", aElem("style", aText("a > b"))))),
	et("<math><annotation-xml encoding=\"text/html\"><style>a > b</style></annotation-xml></math>",
		aElem("math", aElem("annotation-xml", aAttr("encoding", aText("text/html")),
			aElem("style", aText("a > b"))))),
	et("<math><annotation-xml><style>a &gt; b</style></annotation-xml></math>",
		aElem("math", aElem("annotation-xml", aElem("style", aText("a > b"))))),
	et("<math><annotation-xml><svg><style>a &gt; b</style><desc><style>a > b</style></desc></svg></annotation-xml></math>",
		aElem("math", aElem("annotation-xml", aElem("svg",
			aElem("style", aText("a > b")), aElem("desc", aElem("style", aText("a > b"))))))),
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

func TestInvalidReference(t *testing.T) {
	for _, name := range []string{"", "&", "1", "a<b>", "#x", "#12a"} {
		sb := &strings.Builder{}
		if err := NewTreeWriter(sb).WriteAST([]ast.Node{aRef(name)}); err == nil {
			t.Errorf("reference %q: expected error, got output %q", name, sb.String())
		}
	}
}

// Raw text that the HTML parser would not read back as the element's content is an error.
func TestInvalidRawText(t *testing.T) {
	for _, elt := range []ast.Element{
		aElem("script", aText("a</script>b")),
		aElem("script", aText("</SCRIPT x")),
		aElem("script", aText("</script/")),
		aElem("script", aText("</script\n")),
		aElem("style", aText("</Style\t")),
		aElem("script", aText("a</scr"), aText("ipt>")),
		aElem("script", aText("<!--</script>-->")),
		aElem("script", aText("<!--<script>")),
		aElem("script", aElem("b")),
		aElem("style", aRef("lt")),
		aElem("script", aComment("c")),
	} {
		sb := &strings.Builder{}
		if err := NewTreeWriter(sb).WriteAST([]ast.Node{elt}); err == nil {
			t.Errorf("%v: expected error, got output %q", elt, sb.String())
		}
	}
}

func TestInvalidDoctype(t *testing.T) {
	for _, ns := range [][]ast.Node{
		{ast.NewDoctype("xml")},
		{aText("x"), ast.NewDoctype("html")},
		{aElem("p", ast.NewDoctype("html"))},
	} {
		sb := &strings.Builder{}
		if err := NewTreeWriter(sb).WriteAST(ns); err == nil {
			t.Errorf("%v: expected error, got output %q", ns, sb.String())
		}
	}
}
