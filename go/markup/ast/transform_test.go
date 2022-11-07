package ast

import (
	"testing"
)

type matcherTransformerTest struct {
	ins, ons []Node
}

var matcherTransformerTests = []matcherTransformerTest{
	{[]Node{}, []Node{}},

	{ // no matchers to transform
		[]Node{NewText("abc")},
		[]Node{NewText("abc")},
	},

	{ // already valid matchertext
		[]Node{NewText("a(b)c{d}e[f]g")},
		[]Node{NewText("a(b)c{d}e[f]g")},
	},

	{ // valid matchertext with non-Text nodes
		[]Node{NewText("a(b[c{d"), NewReference("foo"),
			NewText("e}f]g)h")},
		[]Node{NewText("a(b[c{d"), NewReference("foo"),
			NewText("e}f]g)h")},
	},

	{ // unmatched matchers interspersed with other text
		[]Node{NewText("a)b]c}d{e[f(g")},
		[]Node{NewText("a"), NewReference("#41"),
			NewText("b"), NewReference("#93"),
			NewText("c"), NewReference("#125"),
			NewText("d"), NewReference("#123"),
			NewText("e"), NewReference("#91"),
			NewText("f"), NewReference("#40"),
			NewText("g")},
	},

	{ // unmatched matchers with Text and non-Text nodes
		[]Node{NewText("a)b]c}d"), NewReference("foo"),
			NewText("e{f[g(h")},
		[]Node{NewText("a"), NewReference("#41"),
			NewText("b"), NewReference("#93"),
			NewText("c"), NewReference("#125"),
			NewText("d"), NewReference("foo"),
			NewText("e"), NewReference("#123"),
			NewText("f"), NewReference("#91"),
			NewText("g"), NewReference("#40"),
			NewText("h")},
	},
}

func TestMatcherTransformer(t *testing.T) {
	xform := &MatcherTransformer{}
	for i, mtt := range matcherTransformerTests {
		ins := make([]Node, len(mtt.ins))
		copy(ins, mtt.ins)
		ons, err := xform.Transform(ins)
		if err != nil {
			t.Errorf("%v: %v", i, err.Error())
			continue
		}
		if !Equal(ons, mtt.ons) {
			t.Errorf("%v: expected %v got %v", i, mtt.ons, ons)
		}
	}
}

// XXX test alternate escaper functions
