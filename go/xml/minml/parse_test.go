package minml

import (
	"strings"
	"testing"

	"github.com/dedis/matchertext.git/go/xml/ast"
)

func aText(s string) ast.Text {
	return ast.Text{Text: s, Raw: false}
}

func aRawText(s string) ast.Text {
	return ast.Text{Text: s, Raw: true}
}

func aRef(name string) ast.Reference {
	return ast.Reference{Name: name}
}

func aAttr(name string, ns ...ast.Node) ast.Attribute {
	return ast.Attribute{Name: name, Value: ns}
}

func aElem(name string, ns ...ast.Node) ast.Element {
	var as []ast.Attribute
	for len(ns) > 0 {
		if a, ok := ns[0].(ast.Attribute); ok {
			as = append(as, a)
			ns = ns[1:]
		} else {
			break
		}
	}
	return ast.Element{Name: name, Attribs: as, Content: ns}
}

type testCase struct {
	s string
	n []ast.Node
}

func tc(s string, ns ...ast.Node) testCase {
	return testCase{s, ns}
}

var parserTests = []testCase{

	// Literal text
	{"", []ast.Node{}},
	tc("foo", aText("foo")),
	tc("a(b)c", aText("a(b)c")),
	tc("[ x ]", aText("[ x ]")),
	tc("[xx ]", aText("[xx ]")),
	tc("[ xx]", aText("[ xx]")),
	tc("[> x]", aText("[x]")),
	tc("[x <]", aText("[x]")),
	tc("[> x <]", aText("[x]")),
	tc("()[]{}", aText("()[]{}")),
	tc("> <(> <)> <", aText("> <(> <)> <")),
	tc("> <{> <}> <", aText("> <{> <}> <")),
	tc("> <[> <]> <", aText(">[]<")),
	tc("([{x}])", aText("([{x}])")),
	tc("a(b"),  // bad matchertext: unmatched opener
	tc("b)c"),  // bad matchertext: unmatched closer
	tc("a[b"),  // bad matchertext: unmatched opener
	tc("a]b"),  // bad matchertext: unmatched closer
	tc("a{b"),  // bad matchertext: unmatched opener
	tc("a}b"),  // bad matchertext: unmatched closer
	tc("a(]b"), // bad matchertext: mismatched matchers
	tc("a{)b"), // bad matchertext: mismatched matchers

	// Character references
	{"[amp]", []ast.Node{aRef("amp")}},
	//XXX	{"[#123]", []ast.Node{aRef("#123")}},
	//XXX	{"[#x12ab]", []ast.Node{aRef("#x12ab")}},
	{" [amp]", []ast.Node{aText(" "), aRef("amp")}},
	{"<[amp]", []ast.Node{aText("<"), aRef("amp")}},
	{"x<[amp]", []ast.Node{aText("x<"), aRef("amp")}},
	{"x <[amp]", []ast.Node{aText("x"), aRef("amp")}},
	{"[amp] ", []ast.Node{aRef("amp"), aText(" ")}},
	{"[amp]>", []ast.Node{aRef("amp"), aText(">")}},
	{"[amp]>x", []ast.Node{aRef("amp"), aText(">x")}},
	{"[amp]> x", []ast.Node{aRef("amp"), aText("x")}},
	{"([amp])", []ast.Node{aText("("), aRef("amp"), aText(")")}},
	{"[[amp]]", []ast.Node{aText("["), aRef("amp"), aText("]")}},
	{"{[amp]}", []ast.Node{aText("{"), aRef("amp"), aText("}")}},
	{"(\t<[amp]>\n)", []ast.Node{aText("("), aRef("amp"), aText(")")}},
	{"[\r\n<[amp]>\n\r]", []ast.Node{aText("["), aRef("amp"), aText("]")}},
	{"{ \t\n<[amp]>\n\t }", []ast.Node{aText("{"), aRef("amp"),
		aText("}")}},

	// Elements
	{"p[]", []ast.Node{aElem("p")}},
	{" p[]", []ast.Node{aText(" "), aElem("p")}},
	{"p[] ", []ast.Node{aElem("p"), aText(" ")}},
	{" <p[]", []ast.Node{aElem("p")}},
	{"p[]> ", []ast.Node{aElem("p")}},
	{"x<p[]", []ast.Node{aText("x<"), aElem("p")}},
	{"x <p[]", []ast.Node{aText("x"), aElem("p")}},
	{"p[]>x", []ast.Node{aElem("p"), aText(">x")}},
	{"p[]> x", []ast.Node{aElem("p"), aText("x")}},
	{"p[> ]", []ast.Node{aElem("p")}},
	{"p[ <]", []ast.Node{aElem("p")}},
	{"p[> <]", []ast.Node{aElem("p")}},
	{" <p[> <]> ", []ast.Node{aElem("p")}},
	{"p[> \t\r\n <]", []ast.Node{aElem("p")}},
	{"p[x]", []ast.Node{aElem("p", aText("x"))}},
	{"p[><]", []ast.Node{aElem("p", aText("><"))}},
	{"p[> x]", []ast.Node{aElem("p", aText("x"))}},
	{"p[> <x]", []ast.Node{aElem("p", aText("<x"))}},
	{"p[x <]", []ast.Node{aElem("p", aText("x"))}},
	{"p[x> <]", []ast.Node{aElem("p", aText("x>"))}},
	{"p[>\t\n\r x \t\n\r<]", []ast.Node{aElem("p", aText("x"))}},
	{"\t\n\r <p[> x <]> \t\n\r", []ast.Node{aElem("p", aText("x"))}},
	{"x[y[]]", []ast.Node{aElem("x", aElem("y"))}},
	{" <x[> <y[> <]> <]> ", []ast.Node{aElem("x", aElem("y"))}},

	// Elements with attributes
	tc("p{}[]", aElem("p")),
	tc("p{"),       // error: unmatched close brace
	tc("p{}"),      // error: missing content
	tc("p{}["),     // error: unmatched open brace
	tc("p{a}[]"),   // error: missing value
	tc("p{> }"),    // error: name expected
	tc("p{ <}"),    // error: name expected
	tc("p{a}> []"), // error: expected value
	tc("p{a} <[]"), // error: expected value
	{"p{a=}[]", []ast.Node{aElem("p", aAttr("a"))}},
	{"p{ a= }[]", []ast.Node{aElem("p", aAttr("a"))}},
	{"p{a=x}[]", []ast.Node{aElem("p", aAttr("a", aText("x")))}},
	{"p{a=x b=y}[]", []ast.Node{aElem("p",
		aAttr("a", aText("x")), aAttr("b", aText("y")))}},
	{"p{a=[]}[]", []ast.Node{aElem("p", aAttr("a"))}},
	{"p{ a=[] }[]", []ast.Node{aElem("p", aAttr("a"))}},
	{"p{ a= <[]}[]", nil}, // error: name expected
	{"p{ a=[]> }[]", nil}, // error: garbage following value
	{" <p{a=[>  <]}[> <]> ", []ast.Node{aElem("p", aAttr("a"))}},
	{"p{a=[x]}[]", []ast.Node{aElem("p", aAttr("a", aText("x")))}},
	{"p{a=[> x <]}[]", []ast.Node{aElem("p", aAttr("a", aText("x")))}},
	{"p{a=[x] b=[y]}[]", []ast.Node{aElem("p",
		aAttr("a", aText("x")), aAttr("b", aText("y")))}},
	{"p{a=[[x]]}[]", []ast.Node{aElem("p", aAttr("a", aRef("x")))}},
	{"p{a=[x[y]]}[]", []ast.Node{aElem("p",
		aAttr("a", aText("x"), aRef("y")))}},
	{"p{a=[x <[> y <]> ]}[]", []ast.Node{aElem("p",
		aAttr("a", aText("x[y]")))}},
	{"p{a=[x]y}[]", nil}, // error: garbage after quoted value
	{"p{a=[ x ]}[]", []ast.Node{aElem("p", aAttr("a", aText(" x ")))}},
	{"p{a=[> x <]}[]", []ast.Node{aElem("p", aAttr("a", aText("x")))}},
	{"p{a=(x)}[]", []ast.Node{aElem("p", aAttr("a", aText("(x)")))}},
	{"p{a={x}}[]", []ast.Node{aElem("p", aAttr("a", aText("{x}")))}},
	{"p{a=[x y]}[]", []ast.Node{aElem("p", aAttr("a", aText("x y")))}},
	{"p{a=(x y)}[]", []ast.Node{aElem("p", aAttr("a", aText("(x y)")))}},
	{"p{a={x y}}[]", []ast.Node{aElem("p", aAttr("a", aText("{x y}")))}},
	{"p{a= }[]", []ast.Node{aElem("p", aAttr("a"))}},
	{"p{a=x }[]", []ast.Node{aElem("p", aAttr("a", aText("x")))}},
	tc("p{ a= }[]", aElem("p", aAttr("a"))),
	tc("p{ a=x }[]", aElem("p", aAttr("a", aText("x")))),
	tc("p{a=x b}[]"),   // error: missing value
	tc("p{ a=x b }[]"), // error: missing value
	tc("p{ a=x b }[]"), // error: missing value

	// Raw text
	{"+[]", []ast.Node{}},
	tc("+[x]", aRawText("x")),
	tc("+[p[]]", aRawText("p[]")),
	tc("+[p[x]]", aRawText("p[x]")),
	tc("+[[x]]", aRawText("[x]")),
	tc("+[+[x]]", aRawText("+[x]")),
	tc("+[x[y]z]", aRawText("x[y]z")),
	tc("+[() <[> <]> {}]", aRawText("() <[> <]> {}")),
	tc(" <+[x]> ", aRawText("x")),
	tc("a <+[x]> b", aText("a"), aRawText("x"), aText("b")),
}

func TestParser(t *testing.T) {
	for i, pt := range parserTests {
		n, e := Parse(strings.NewReader(pt.s))
		if e != nil && pt.n != nil {
			t.Errorf("%v '%v': %v", i, pt.s, e.Error())
		} else if e == nil && pt.n == nil {
			t.Errorf("%v '%v': expected error, got %v", i, pt.s, n)
		} else if e == nil && pt.n != nil && !ast.DeepEqual(n, pt.n) {
			t.Errorf("%v '%v': wrong output %v", i, pt.s, n)
		}
	}
}
