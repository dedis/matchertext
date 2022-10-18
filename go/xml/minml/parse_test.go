package minml

import (
	"strings"
	"testing"

	"github.com/dedis/matchertext.git/go/xml/ast"
)

// The following are convenience constructor functions for AST nodes.

func aText(s string) ast.Text {
	return ast.Text{Text: s, Raw: false}
}

func aRawText(s string) ast.Text {
	return ast.Text{Text: s, Raw: true}
}

func aComment(s string) ast.Comment {
	return ast.Comment{Text: s}
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
	s string     // MinML string to be parsed
	n []ast.Node // AST that it should parse to
}

// Convenience function to construct a testCase.
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
	tc("[amp]", aRef("amp")),
	tc("[#123]", aRef("#123")),
	tc("[#x12ab]", aRef("#x12ab")),
	tc(" [amp]", aText(" "), aRef("amp")),
	tc("<[amp]", aText("<"), aRef("amp")),
	tc("x<[amp]", aText("x<"), aRef("amp")),
	tc("x <[amp]", aText("x"), aRef("amp")),
	tc("[amp] ", aRef("amp"), aText(" ")),
	tc("[amp]>", aRef("amp"), aText(">")),
	tc("[amp]>x", aRef("amp"), aText(">x")),
	tc("[amp]> x", aRef("amp"), aText("x")),
	tc("([amp])", aText("("), aRef("amp"), aText(")")),
	tc("[[amp]]", aText("["), aRef("amp"), aText("]")),
	tc("{[amp]}", aText("{"), aRef("amp"), aText("}")),
	tc("(\t<[amp]>\n)", aText("("), aRef("amp"), aText(")")),
	tc("[\r\n<[amp]>\n\r]", aText("["), aRef("amp"), aText("]")),
	tc("{ \t\n<[amp]>\n\t }", aText("{"), aRef("amp"), aText("}")),
	tc("[]", aText("[]")),       // not a character reference
	tc("[?]", aText("[?]")),     // not a character reference
	tc("[#]", aText("[#]")),     // not a character reference
	tc("[#a]", aText("[#a]")),   // not a character reference
	tc("[#x]", aText("[#x]")),   // not a character reference
	tc("[#x@]", aText("[#x@]")), // not a character reference
	tc("[#xg]", aText("[#xg]")), // not a character reference

	// Elements
	tc("p[]", aElem("p")),
	tc(" p[]", aText(" "), aElem("p")),
	tc("p[] ", aElem("p"), aText(" ")),
	tc(" <p[]", aElem("p")),
	tc("p[]> ", aElem("p")),
	tc("x<p[]", aText("x<"), aElem("p")),
	tc("x <p[]", aText("x"), aElem("p")),
	tc("p[]>x", aElem("p"), aText(">x")),
	tc("p[]> x", aElem("p"), aText("x")),
	tc("p[> ]", aElem("p")),
	tc("p[ <]", aElem("p")),
	tc("p[> <]", aElem("p")),
	tc(" <p[> <]> ", aElem("p")),
	tc("p[> \t\r\n <]", aElem("p")),
	tc("p[x]", aElem("p", aText("x"))),
	tc("p[><]", aElem("p", aText("><"))),
	tc("p[> x]", aElem("p", aText("x"))),
	tc("p[> <x]", aElem("p", aText("<x"))),
	tc("p[x <]", aElem("p", aText("x"))),
	tc("p[x> <]", aElem("p", aText("x>"))),
	tc("p[>\t\n\r x \t\n\r<]", aElem("p", aText("x"))),
	tc("\t\n\r <p[> x <]> \t\n\r", aElem("p", aText("x"))),
	tc("x[y[]]", aElem("x", aElem("y"))),
	tc(" <x[> <y[> <]> <]> ", aElem("x", aElem("y"))),

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
	tc("p{a=}[]", aElem("p", aAttr("a"))),
	tc("p{ a= }[]", aElem("p", aAttr("a"))),
	tc("p{a=x}[]", aElem("p", aAttr("a", aText("x")))),
	tc("p{a=x b=y}[]", aElem("p",
		aAttr("a", aText("x")), aAttr("b", aText("y")))),
	tc("p{a=[]}[]", aElem("p", aAttr("a"))),
	tc("p{ a=[] }[]", aElem("p", aAttr("a"))),
	tc("p{ a= <[]}[]"), // error: name expected
	tc("p{ a=[]> }[]"), // error: garbage following value
	tc(" <p{a=[>  <]}[> <]> ", aElem("p", aAttr("a"))),
	tc("p{a=[x]}[]", aElem("p", aAttr("a", aText("x")))),
	tc("p{a=[> x <]}[]", aElem("p", aAttr("a", aText("x")))),
	tc("p{a=[x] b=[y]}[]", aElem("p",
		aAttr("a", aText("x")), aAttr("b", aText("y")))),
	tc("p{a=[[x]]}[]", aElem("p", aAttr("a", aRef("x")))),
	tc("p{a=[x[y]]}[]", aElem("p", aAttr("a", aText("x"), aRef("y")))),
	tc("p{a=[x <[> y <]> ]}[]", aElem("p", aAttr("a", aText("x[y]")))),
	tc("p{a=[x]y}[]"), // error: garbage after quoted value
	tc("p{a=[ x ]}[]", aElem("p", aAttr("a", aText(" x ")))),
	tc("p{a=[> x <]}[]", aElem("p", aAttr("a", aText("x")))),
	tc("p{a=(x)}[]", aElem("p", aAttr("a", aText("(x)")))),
	tc("p{a={x}}[]", aElem("p", aAttr("a", aText("{x}")))),
	tc("p{a=[x y]}[]", aElem("p", aAttr("a", aText("x y")))),
	tc("p{a=(x y)}[]", aElem("p", aAttr("a", aText("(x y)")))),
	tc("p{a={x y}}[]", aElem("p", aAttr("a", aText("{x y}")))),
	tc("p{a= }[]", aElem("p", aAttr("a"))),
	tc("p{a=x }[]", aElem("p", aAttr("a", aText("x")))),
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

	// Comments
	{"-[]", []ast.Node{}},
	tc("-[x]", aComment("x")),
	tc(" <-[x]> ", aComment("x")),
	tc("-[> abc <]", aComment("> abc <")),
	tc("-[> ({[]}) <]", aComment("> ({[]}) <")),
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
