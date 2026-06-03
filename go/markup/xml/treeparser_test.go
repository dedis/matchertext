package xml

import (
	"strings"
	"testing"

	"github.com/dedis/matchertext/go/markup/ast"
)

type parseTest struct {
	xml string
	ast []ast.Node
}

func pt(xmlStr string, ns ...ast.Node) parseTest {
	return parseTest{xml: xmlStr, ast: ns}
}

var parseTests = []parseTest{

	// Empty input
	pt(""),
	pt("   ", aText("   ")),
	pt("<?xml version=\"1.0\"?>"),

	// Plain text
	pt("hello", aText("hello")),

	// Comments
	pt("<!-- hi -->", aComment(" hi ")),
	pt("<!-- a --><!-- b -->", aComment(" a "), aComment(" b ")),

	// Simple elements
	pt("<p/>", aElem("p")),
	pt("<br/>", aElem("br")),
	pt("<em>emphasis</em>", aElem("em", aText("emphasis"))),
	pt("<i><b>nested</b></i>", aElem("i", aElem("b", aText("nested")))),

	// Self-closing with whitespace-only text around it
	pt(" <p/> ", aText(" "), aElem("p"), aText(" ")),

	// Elements with attributes
	pt(`<a href="foo">link</a>`,
		aElem("a", aAttr("href", aText("foo")), aText("link"))),
	pt(`<img src="foo" alt="bar"/>`,
		aElem("img", aAttr("src", aText("foo")), aAttr("alt", aText("bar")))),

	// Namespace prefix on element and attribute
	pt(`<tei:p xml:lang="en">text</tei:p>`,
		aElem("tei:p", aAttr("xml:lang", aText("en")), aText("text"))),

	// xmlns declarations are dropped
	pt(`<TEI xmlns="http://www.tei-c.org/ns/1.0"><body/></TEI>`,
		aElem("TEI", aElem("body"))),

	// Mixed content — representative TEI fragment
	pt(`<p>I met <persName ref="#JohnSmith">John</persName>.</p>`,
		aElem("p",
			aText("I met "),
			aElem("persName", aAttr("ref", aText("#JohnSmith")), aText("John")),
			aText("."),
		)),

	// Self-closing element with attributes (lb, gap)
	pt(`<lb n="4"/>`, aElem("lb", aAttr("n", aText("4")))),
	pt(`<gap reason="damage" extent="2" unit="chars"/>`,
		aElem("gap",
			aAttr("reason", aText("damage")),
			aAttr("extent", aText("2")),
			aAttr("unit", aText("chars")),
		)),

	// DOCTYPE and processing instructions are skipped
	pt("<!DOCTYPE foo []><p/>", aElem("p")),
	pt("<?xml version=\"1.0\"?><p/>", aElem("p")),
}

func TestTreeParser(t *testing.T) {
	for i, pt := range parseTests {
		d := NewTreeParser(strings.NewReader(pt.xml))
		got, err := d.ParseAST()
		if err != nil {
			t.Errorf("%v %q: unexpected error: %v", i, pt.xml, err)
			continue
		}
		want := pt.ast
		if want == nil {
			want = []ast.Node{}
		}
		if !ast.Equal(got, want) {
			t.Errorf("%v %q:\n  got  %v\n  want %v", i, pt.xml, got, want)
		}
	}
}
