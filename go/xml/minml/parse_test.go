package minml

import (
	"strings"
	"testing"

	"github.com/dedis/matchertext.git/go/xml/ast"
)

var parserTests = []struct {
	s string
	n []ast.Node
}{

	// Literal text
	{"", []ast.Node{}},
	{"foo", []ast.Node{ast.Text{"foo"}}},
	{"a(b)c", []ast.Node{ast.Text{"a(b)c"}}},
	{"[ x ]", []ast.Node{ast.Text{"[ x ]"}}},
	{"[xx ]", []ast.Node{ast.Text{"[xx ]"}}},
	{"[ xx]", []ast.Node{ast.Text{"[ xx]"}}},
	{"[> x]", []ast.Node{ast.Text{"[x]"}}},
	{"[x <]", []ast.Node{ast.Text{"[x]"}}},
	{"[> x <]", []ast.Node{ast.Text{"[x]"}}},
	{"()[]{}", []ast.Node{ast.Text{"()[]{}"}}},
	{"([{x}])", []ast.Node{ast.Text{"([{x}])"}}},
	{"a(b", nil},  // bad matchertext: unmatched opener
	{"b)c", nil},  // bad matchertext: unmatched closer
	{"a[b", nil},  // bad matchertext: unmatched opener
	{"a]b", nil},  // bad matchertext: unmatched closer
	{"a{b", nil},  // bad matchertext: unmatched opener
	{"a}b", nil},  // bad matchertext: unmatched closer
	{"a(]b", nil}, // bad matchertext: mismatched matchers
	{"a{)b", nil}, // bad matchertext: mismatched matchers

	// Character references
	{"[amp]", []ast.Node{ast.Reference{"amp"}}},
	//XXX	{"[#123]", []ast.Node{ast.Reference{"#123"}}},
	//XXX	{"[#x12ab]", []ast.Node{ast.Reference{"#x12ab"}}},
	{" [amp]", []ast.Node{ast.Text{" "}, ast.Reference{"amp"}}},
	{"<[amp]", []ast.Node{ast.Text{"<"}, ast.Reference{"amp"}}},
	{"x<[amp]", []ast.Node{ast.Text{"x<"}, ast.Reference{"amp"}}},
	{"x <[amp]", []ast.Node{ast.Text{"x"}, ast.Reference{"amp"}}},
	{"[amp] ", []ast.Node{ast.Reference{"amp"}, ast.Text{" "}}},
	{"[amp]>", []ast.Node{ast.Reference{"amp"}, ast.Text{">"}}},
	{"[amp]>x", []ast.Node{ast.Reference{"amp"}, ast.Text{">x"}}},
	{"[amp]> x", []ast.Node{ast.Reference{"amp"}, ast.Text{"x"}}},
	{"([amp])", []ast.Node{ast.Text{"("}, ast.Reference{"amp"},
		ast.Text{")"}}},
	{"[[amp]]", []ast.Node{ast.Text{"["}, ast.Reference{"amp"},
		ast.Text{"]"}}},
	{"{[amp]}", []ast.Node{ast.Text{"{"}, ast.Reference{"amp"},
		ast.Text{"}"}}},
	{"(\t<[amp]>\n)", []ast.Node{ast.Text{"("}, ast.Reference{"amp"},
		ast.Text{")"}}},
	{"[\r\n<[amp]>\n\r]", []ast.Node{ast.Text{"["}, ast.Reference{"amp"},
		ast.Text{"]"}}},
	{"{ \t\n<[amp]>\n\t }", []ast.Node{ast.Text{"{"}, ast.Reference{"amp"},
		ast.Text{"}"}}},

	// Elements
	{"p[]", []ast.Node{ast.Element{"p", nil, nil}}},
	{" p[]", []ast.Node{ast.Text{" "}, ast.Element{"p", nil, nil}}},
	{"p[] ", []ast.Node{ast.Element{"p", nil, nil}, ast.Text{" "}}},
	{" <p[]", []ast.Node{ast.Element{"p", nil, nil}}},
	{"p[]> ", []ast.Node{ast.Element{"p", nil, nil}}},
	{"x<p[]", []ast.Node{ast.Text{"x<"}, ast.Element{"p", nil, nil}}},
	{"x <p[]", []ast.Node{ast.Text{"x"}, ast.Element{"p", nil, nil}}},
	{"p[]>x", []ast.Node{ast.Element{"p", nil, nil}, ast.Text{">x"}}},
	{"p[]> x", []ast.Node{ast.Element{"p", nil, nil}, ast.Text{"x"}}},
	{"p[> <]", []ast.Node{ast.Element{"p", nil, nil}}},
	{"p[> \t\r\n <]", []ast.Node{ast.Element{"p", nil, nil}}},
	{"p[x]", []ast.Node{ast.Element{"p", nil,
		[]ast.Node{ast.Text{"x"}}}}},
	{"p[><]", []ast.Node{ast.Element{"p", nil,
		[]ast.Node{ast.Text{"><"}}}}},
	{"p[> x]", []ast.Node{ast.Element{"p", nil,
		[]ast.Node{ast.Text{"x"}}}}},
	{"p[> <x]", []ast.Node{ast.Element{"p", nil,
		[]ast.Node{ast.Text{"<x"}}}}},
	{"p[x <]", []ast.Node{ast.Element{"p", nil,
		[]ast.Node{ast.Text{"x"}}}}},
	{"p[x> <]", []ast.Node{ast.Element{"p", nil,
		[]ast.Node{ast.Text{"x>"}}}}},
	{"p[>\t\n\r x \t\n\r<]", []ast.Node{ast.Element{"p", nil,
		[]ast.Node{ast.Text{"x"}}}}},
	{"\t\n\r <p[> x <]> \t\n\r", []ast.Node{ast.Element{"p", nil,
		[]ast.Node{ast.Text{"x"}}}}},
	{"x[y[]]", []ast.Node{ast.Element{"x", nil,
		[]ast.Node{ast.Element{"y", nil, nil}}}}},
	{" <x[> <y[> <]> <]> ", []ast.Node{ast.Element{"x", nil,
		[]ast.Node{ast.Element{"y", nil, nil}}}}},

	// Elements with attributes
	{"p{}[]", []ast.Node{ast.Element{"p", nil, nil}}},
	{"p{", nil},     // error: unmatched close brace
	{"p{}", nil},    // error: missing content
	{"p{}[", nil},   // error: unmatched open brace
	{"p{a}[]", nil}, // error: missing value
	{"p{> }", nil},   // error: name expected
	{"p{ <}", nil},   // error: name expected
	{"p{a=}[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a", nil}}, nil}}},
	{"p{a=x}[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Text{"x"}}}}, nil}}},
	{"p{a=x b=y}[]", []ast.Node{ast.Element{"p", []ast.Attribute{
		ast.Attribute{"a", []ast.Node{ast.Text{"x"}}},
		ast.Attribute{"b", []ast.Node{ast.Text{"y"}}}}, nil}}},
	{"p{a=[x]}[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Text{"x"}}}}, nil}}},
	{"p{a=[x] b=[y]}[]", []ast.Node{ast.Element{"p", []ast.Attribute{
		ast.Attribute{"a", []ast.Node{ast.Text{"x"}}},
		ast.Attribute{"b", []ast.Node{ast.Text{"y"}}}}, nil}}},
	{"p{a=[[x]]}[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Reference{"x"}}}}, nil}}},
	{"p{a=[x[y]]}[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Text{"x"}, ast.Reference{"y"}}}}, nil}}},
	{"p{a=[x <[> y <]> ]}[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Text{"x[y]"}}}}, nil}}},
	{"p{a=[x]y}[]", nil}, // error: garbage after quoted value
	{"p{a=[ x ]}[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Text{" x "}}}}, nil}}},
	{"p{a=[> x <]}[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Text{"x"}}}}, nil}}},
	{"p{a=(x)}[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Text{"(x)"}}}}, nil}}},
	{"p{a={x}}[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Text{"{x}"}}}}, nil}}},
	{"p{a= }[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a", nil}}, nil}}},
	{"p{a=x }[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Text{"x"}}}}, nil}}},
	{"p{ a= }[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a", nil}}, nil}}},
	{"p{ a=x }[]", []ast.Node{ast.Element{"p",
		[]ast.Attribute{ast.Attribute{"a",
			[]ast.Node{ast.Text{"x"}}}}, nil}}},
	{"p{a=x b}[]", nil},   // error: missing value
	{"p{ a=x b }[]", nil}, // error: missing value
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
