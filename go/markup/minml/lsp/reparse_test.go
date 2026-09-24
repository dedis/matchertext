package lsp

import (
	"fmt"
	"math/rand"
	"reflect"
	"sort"
	"strings"
	"testing"
)

// randomMinML returns a document with nested elements, attributes, and random noise.
func randomMinML(rng *rand.Rand, depth int) string {
	pieces := []string{"a", "é", " ", "\n", "\r\n", "[amp]", "[1]", "-[c (x)]", "+[r]", `"[q]`,
		"(", ")", "[", "]", "{", "}", "<", ">", "="}
	var b strings.Builder
	for k := rng.Intn(6); k > 0; k-- {
		switch {
		case depth > 0 && rng.Intn(3) == 0:
			b.WriteString([]string{"p", "div", "x"}[rng.Intn(3)])
			if rng.Intn(3) == 0 {
				b.WriteString("{a=b c=[d [amp]]}")
			}
			b.WriteString("[" + randomMinML(rng, depth-1) + "]")
		case rng.Intn(4) == 0:
			b.WriteString(pieces[rng.Intn(len(pieces))])
		default:
			b.WriteString([]string{"text ", "q[w] ", "(y) "}[rng.Intn(3)])
		}
	}
	return b.String()
}

func sortedProblems(ps []Problem) []Problem {
	ps = append([]Problem(nil), ps...)
	sort.Slice(ps, func(i, j int) bool { return fmt.Sprint(ps[i]) < fmt.Sprint(ps[j]) })
	return ps
}

func checkSameDocument(t *testing.T, what string, got, want *Document) {
	t.Helper()
	fail := func(field string, g, w any) {
		t.Fatalf("%s\ntext %q\n%s: got  %v\n%s: want %v", what, want.Text, field, g, field, w)
	}
	switch {
	case !reflect.DeepEqual(got.lines, want.lines):
		fail("lines", got.lines, want.lines)
	case !reflect.DeepEqual(got.Marks, want.Marks):
		fail("marks", got.Marks, want.Marks)
	case !reflect.DeepEqual(got.Blocks, want.Blocks) && len(got.Blocks)+len(want.Blocks) > 0:
		fail("blocks", got.Blocks, want.Blocks)
	case !reflect.DeepEqual(sortedProblems(got.Problems), sortedProblems(want.Problems)) &&
		len(got.Problems)+len(want.Problems) > 0:
		fail("problems", got.Problems, want.Problems)
	case !reflect.DeepEqual(got.Syncs, want.Syncs) && len(got.Syncs)+len(want.Syncs) > 0:
		fail("syncs", got.Syncs, want.Syncs)
	}
}

func TestReparseMatchesFullParse(t *testing.T) {
	inserts := []string{"", "a", "x[", "]", "p[y]", "[", "{", "}", "(", ")", "\n", "\r", "é", " <", "> ",
		"-[", "+[", "[amp]", "=", "div{a=", "q[w] ", "]]", "[["}
	rng := rand.New(rand.NewSource(3))
	for n := 0; n < 20000; n++ {
		d := newDocument(randomMinML(rng, 4), 0, nil)
		for k := 0; k < 4; k++ {
			s := rng.Intn(len(d.Text) + 1)
			e := s + rng.Intn(min(len(d.Text)-s, 6)+1)
			ins := inserts[rng.Intn(len(inserts))]
			what := fmt.Sprintf("edit [%d,%d) -> %q of %q", s, e, ins, d.Text)
			d = d.edit(s, e, ins, 0)
			checkSameDocument(t, what, d, newDocument(d.Text, 0, nil))
		}
	}
}
