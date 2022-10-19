package ast

import (
	"strings"
	"testing"
)

type encTest struct {
	ast []Node
	out string
}

func et(out string, ns ...Node) encTest {
	return encTest{ast: ns, out: out}
}

var encTests = []encTest{

	// Simple text
	et(""),
	et("abc", NewText("abc", false)),
	et("abcxyz", NewText("abc", false), NewText("xyz", false)),
	et("x'\"\r\n\ty", NewText("x'\"\r\n\ty", false)),
	et("a&lt;b&gt;c&amp;d'e\"f", NewText("a<b>c&d'e\"f", false)),

	// Raw text
	et("<![CDATA[abc]]>", NewText("abc", true)),
	et("<![CDATA[&foo;]]>", NewText("&foo;", true)),
	et("<![CDATA[<mark></up>]]>", NewText("<mark></up>", true)),
	et("<![CDATA[]]]]><![CDATA[>]]>", NewText("]]>", true)),
	et("<![CDATA[example <![CDATA[character data]]]]><![CDATA[> section]]>",
		NewText("example <![CDATA[character data]]> section", true)),

	// References
	et("&hello;", NewReference("hello")),
	et("&#123;", NewReference("#123")),
	et("&#xabcd;", NewReference("#xabcd")),

	// Elements
	et("<p/>", NewElement("p")),
	et("<br/>", NewElement("br")),
	et("<em>emphasis</em>", NewElement("em", NewText("emphasis", false))),
	et("<i><b>nested</b></i>",
		NewElement("i", NewElement("b", NewText("nested", false)))),
	et("<a href=\"foo\">link</a>", NewElement("a",
		NewAttribute("href", NewText("foo", false)),
		NewText("link", false))),
	et("<img src=\"foo\" alt=\"bar\"/>", NewElement("img",
		NewAttribute("src", NewText("foo", false)),
		NewAttribute("alt", NewText("bar", false)))),
	et("<x y=\"&amp;&lt;&gt;&quot;'\"/>", NewElement("x",
		NewAttribute("y", NewText("&<>\"'", false)))),
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
