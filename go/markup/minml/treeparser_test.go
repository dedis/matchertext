package minml

import (
	"strings"
	"testing"

	"github.com/dedis/matchertext/go/markup/ast"
)

type testCase struct {
	s string     // MinML string to be parsed
	n []ast.Node // AST that it should parse to
}

// Convenience function to construct a testCase.
func tc(s string, ns ...ast.Node) testCase {
	return testCase{s, ns}
}

var decodeTests = []testCase{

	// Literal text
	{"", []ast.Node{}},
	tc("foo", aText("foo")),
	tc("a(b)c", aText("a(b)c")),
	tc("[]", aText("[]")),
	tc("[ x ]", aText("[ x ]")),
	tc("[xx ]", aText("[xx ]")),
	tc("[ xx]", aText("[ xx]")),
	tc("[> x]", aText("[x]")),
	tc("[x <]", aText("[x]")),
	tc("[> x <]", aText("[x]")),
	tc("()[]{}", aText("()[]{}")),
	tc(" [ [ ] ] ", aText(" [ [ ] ] ")),
	tc("a <(x y)", aText("a <(x y)")),           // no space-sucking
	tc("a <[x y]", aText("a[x y]")),             // space-sucking
	tc("a <{x y}", aText("a{x y}")),             // space-sucking
	tc("> <(> <)> <", aText("> <(> <)> <")),     // no space-sucking
	tc("> <{> <}> <", aText(">{}<")),            // space-sucking
	tc("> <[> <]> <", aText(">[]<")),            // space-sucking
	tc("> <(> x <)> <", aText("> <(> x <)> <")), // no space-sucking
	tc("> <{> x <}> <", aText(">{x}<")),         // space-sucking
	tc("> <[> x <]> <", aText(">[x]<")),         // space-sucking
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
	tc("[?]", aRef("?")),
	tc("[#]", aRef("#")),
	tc("[#123]", aRef("#123")),
	tc("[#a]", aRef("#a")),
	tc("[#x]", aRef("#x")),
	tc("[#x12ab]", aRef("#x12ab")),
	tc("[#x@]", aRef("#x@")),
	tc("[#xg]", aRef("#xg")),

	// Elements
	tc("p[]", aElem("p")),
	tc("p[q]", aElem("p", aText("q"))),
	tc("*[]", aElem("*")),
	tc("*[+]", aElem("*", aText("+"))),
	tc(" p[]", aText(" "), aElem("p")),
	tc("p[] ", aElem("p"), aText(" ")),
	tc(" <p[]", aElem("p")),
	tc(" <<[]", aElem("<")),
	tc(" <>[]", aElem(">")),
	tc(" <<<[]", aElem("<<")),
	tc(" <<<>>[]", aElem("<<>>")),
	tc("p[]> ", aElem("p")),
	tc("x<p[]", aElem("x<p")),
	tc("x <p[]", aText("x"), aElem("p")),
	tc("< <p[]", aText("<"), aElem("p")),
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
	for i, dt := range decodeTests {
		d := NewTreeParser(strings.NewReader(dt.s))
		n, e := d.ParseAST()
		if e != nil && dt.n != nil {
			t.Errorf("%v '%v': %v", i, dt.s, e.Error())
		} else if e == nil && dt.n == nil {
			t.Errorf("%v '%v': expected error, got %v", i, dt.s, n)
		} else if e == nil && dt.n != nil && !ast.Equal(n, dt.n) {
			t.Errorf("%v '%v': wrong output %v", i, dt.s, n)
		}
	}
}
