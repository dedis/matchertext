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
		[]Node{NewText("abc", false)},
		[]Node{NewText("abc", false)},
	},

	{ // already valid matchertext
		[]Node{NewText("a(b)c{d}e[f]g", false)},
		[]Node{NewText("a(b)c{d}e[f]g", false)},
	},

	{ // valid matchertext with non-Text nodes
		[]Node{NewText("a(b[c{d", false), NewReference("foo"),
			NewText("e}f]g)h", false)},
		[]Node{NewText("a(b[c{d", false), NewReference("foo"),
			NewText("e}f]g)h", false)},
	},

	{ // unmatched matchers interspersed with other text
		[]Node{NewText("a)b]c}d{e[f(g", false)},
		[]Node{NewText("a", false), NewReference("#41"),
			NewText("b", false), NewReference("#93"),
			NewText("c", false), NewReference("#125"),
			NewText("d", false), NewReference("#123"),
			NewText("e", false), NewReference("#91"),
			NewText("f", false), NewReference("#40"),
			NewText("g", false)},
	},

	{ // unmatched matchers with Text and non-Text nodes
		[]Node{NewText("a)b]c}d", false), NewReference("foo"),
			NewText("e{f[g(h", false)},
		[]Node{NewText("a", false), NewReference("#41"),
			NewText("b", false), NewReference("#93"),
			NewText("c", false), NewReference("#125"),
			NewText("d", false), NewReference("foo"),
			NewText("e", false), NewReference("#123"),
			NewText("f", false), NewReference("#91"),
			NewText("g", false), NewReference("#40"),
			NewText("h", false)},
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
		if !DeepEqual(ons, mtt.ons) {
			t.Errorf("%v: expected %v got %v", i, mtt.ons, ons)
		}
	}
}

// XXX test alternate escaper functions
