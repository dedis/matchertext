package minml

import (
	"testing"
	"strings"

	"github.com/dedis/matchertext.git/go/xml/ast"
)


var parserTests = []struct{ s string; n []ast.Node }{
	{"", []ast.Node{}},
	{"foo", []ast.Node{ast.Text{"foo"}}},
}

func TestParser(t *testing.T) {
	for i, pt := range parserTests {
		n, e := Parse(strings.NewReader(pt.s))
		if e != nil && pt.n != nil {
			t.Errorf("%v: %v", i, e.Error())
		} else if e == nil && pt.n == nil {
			t.Errorf("%v: expected error", i)
		} else if e == nil && pt.n != nil && !ast.DeepEqual(n, pt.n) {
			t.Errorf("%v: wrong output %v", i, n)
		}
	}
}

