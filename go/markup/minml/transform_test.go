package minml

import (
	"strings"
	"testing"

	"github.com/dedis/matchertext/go/markup/ast"
)

var transformTests = []testCase{

	// HTML named entities
	tc("[star][cir][larr]",
		aText("\u2606"), aText("\u25CB"), aText("\u2190")),

	// MinML symbolic entities
	//	tc("[o()][c()][o[]][c[]]",
	//		aText("("), aText(")"),
	//		aText("["), aText("]"),
	//		aText("{"), aText("}")),
	tc("[--][+-][-->]",
		aText("\u2013"), aText("\u00B1"), aText("\u2192")),

	// Quoted strings
	tc("'[quote]", aText("\u2018"), aText("quote"), aText("\u2019")),
	tc("\"[quote]", aText("\u201C"), aText("quote"), aText("\u201D")),
}

func TestTransform(t *testing.T) {
	for i, dt := range transformTests {
		d := NewDecoder(strings.NewReader(dt.s)).
			WithTransformer(EntityTransformer).
			WithTransformer(QuoteTransformer)
		n, e := d.Decode()
		if e != nil && dt.n != nil {
			t.Errorf("%v '%v': %v", i, dt.s, e.Error())
		} else if e == nil && dt.n == nil {
			t.Errorf("%v '%v': expected error, got %v", i, dt.s, n)
		} else if e == nil && dt.n != nil && !ast.DeepEqual(n, dt.n) {
			t.Errorf("%v '%v': wrong output %v", i, dt.s, n)
		}
	}
}
