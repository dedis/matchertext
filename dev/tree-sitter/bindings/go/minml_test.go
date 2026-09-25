package tree_sitter_minml_test

import (
	"fmt"
	"math/rand"
	"os"
	"path/filepath"
	"reflect"
	"regexp"
	"strings"
	"testing"

	tree_sitter_minml "github.com/dedis/matchertext/dev/tree-sitter/bindings/go"
	"github.com/dedis/matchertext/go/markup/minml/lsp"
	tree_sitter "github.com/tree-sitter/go-tree-sitter"
)

var kinds = map[string]lsp.Kind{
	"tag_name":  lsp.KindTag,
	"attr_name": lsp.KindAttrName,
	"reference": lsp.KindReference,
	"comment":   lsp.KindComment,
	"raw":       lsp.KindRaw,
}

// treeMarks returns the constructs of a tree in document order, as the Go parser reports them:
// the Go parser drops empty comments and raw text.
func treeMarks(n *tree_sitter.Node, marks []lsp.Mark) []lsp.Mark {
	if k, ok := kinds[n.Kind()]; ok {
		start, end := int(n.StartByte()), int(n.EndByte())
		if !((k == lsp.KindComment || k == lsp.KindRaw) && end-start == 3) {
			marks = append(marks, lsp.Mark{Start: start, End: end, Kind: k})
		}
	}
	for i := uint(0); i < n.ChildCount(); i++ {
		marks = treeMarks(n.Child(i), marks)
	}
	return marks
}

// check compares the tree-sitter grammar with the Go parser on src:
// both accept it or both reject it, and on accepted documents both report the same constructs.
func check(t *testing.T, parser *tree_sitter.Parser, src string) {
	t.Helper()
	syn := lsp.Parse(src)
	goOK := true
	for _, p := range syn.Problems {
		goOK = goOK && p.Hint
	}
	tree := parser.Parse([]byte(src), nil)
	defer tree.Close()
	root := tree.RootNode()
	if tsOK := !root.HasError(); tsOK != goOK {
		t.Fatalf("%q: Go parser accepts: %v, tree-sitter accepts: %v\nGo problems: %v\ntree: %s",
			src, goOK, tsOK, syn.Problems, root.ToSexp())
	}
	if !goOK {
		return
	}
	got := treeMarks(root, nil)
	if !reflect.DeepEqual(got, syn.Marks) && len(got)+len(syn.Marks) > 0 {
		t.Fatalf("%q:\ntree-sitter %v\nGo         %v\ntree: %s", src, got, syn.Marks, root.ToSexp())
	}
}

func newParser(t *testing.T) *tree_sitter.Parser {
	parser := tree_sitter.NewParser()
	t.Cleanup(parser.Close)
	if err := parser.SetLanguage(tree_sitter.NewLanguage(tree_sitter_minml.Language())); err != nil {
		t.Fatal(err)
	}
	return parser
}

func TestCorpusMatchesGoParser(t *testing.T) {
	parser := newParser(t)
	files, _ := filepath.Glob("../../../../test/*.m")
	more, _ := filepath.Glob("../../../../test/examples/*.m")
	files = append(files, more...)
	if len(files) == 0 {
		t.Fatal("no corpus files found")
	}
	for _, f := range files {
		src, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		check(t, parser, string(src))
	}
}

func TestCasesMatchGoParser(t *testing.T) {
	parser := newParser(t)
	for _, src := range []string{
		"", "p[ok]", "p[see [a b] here]", "p[x (unbalanced]", "p[a {b} c]", `p["[a b[em[x]]]]`,
		"p['[single]]", "p[é[x]]", "p[h1.big[x]]", "p[[&]]", "-[comment (open]", "x <<p[a]",
		"a{href=x.html alt=[a [amp] cat]}[link]", "p{cla", "p{a=b", "p{a=b} x", "p{a=b]", "p{(x)=1}[y]",
		"p{1a=b}[y]", "p{a=[x]y}[z]", "p{a b=c}[d]", "]x]", "[abc", "+[raw <b>]", "-[a\nb]\nc[d]",
		"<p[a]> <q[b]>", "p[[[<]] [(>)]]", "x[y{z}]", "p[\r\nq[a]\r\n]", "-{x}", "+{x}", "a-[x]",
		"<-[x]", "<<[x]", "< [x]", "[<]", "[>]", "[a(b)c]", "p{a=f(x y) b=x[amp]y}[]", "p{=x}[]",
		"p{a=}[] q{a= b=c}[]", "p{}[]", "p{ }[]", "[{<}] [{>}]", "?[pi]", "p[a]]b", "{x}", "(x)",
	} {
		check(t, parser, src)
	}
}

func TestRandomMatchesGoParser(t *testing.T) {
	parser := newParser(t)
	alphabet := []string{"a", "p", "é", "😀", " ", "\n", "[", "]", "{", "}", "(", ")", "<", ">", "-", "+",
		`"`, "'", "=", "#", "&", "?", "x[", "p{a=b}[", "-[", "+[", "[amp]", "[[<]]", "\t", "\r"}
	rng := rand.New(rand.NewSource(1))
	for n := 0; n < 200000; n++ {
		var b strings.Builder
		for k := rng.Intn(20); k > 0; k-- {
			b.WriteString(alphabet[rng.Intn(len(alphabet))])
		}
		check(t, parser, b.String())
	}
}

func ExampleParse() {
	fmt.Println(lsp.Parse("p[x]").Marks)
	// Output: [{0 1 0}]
}

// randomDocument returns mostly valid MinML with nested elements, attributes, and noise.
func randomDocument(rng *rand.Rand, depth int) string {
	pick := func(xs ...string) string { return xs[rng.Intn(len(xs))] }
	var b strings.Builder
	for k := rng.Intn(5); k > 0; k-- {
		switch {
		case depth > 0 && rng.Intn(3) == 0:
			b.WriteString(pick("p", "div", "<em", "a-b", `"`, "'", "?", "h1.x", "é"))
			if rng.Intn(2) == 0 {
				b.WriteString("{" + pick("", " ") + pick("a=b", "c=[d [amp] e]", "f=g(h i)", "j=", "k=x[amp]y", "l=[]") +
					pick("", " m=n", " o=[p q]") + pick("", " ") + "}")
			}
			b.WriteString("[" + randomDocument(rng, depth-1) + "]")
		case rng.Intn(3) == 0:
			b.WriteString(pick("[amp]", "[#174]", "[[<]]", "[(>)]", "-[c (x) [y]]", "+[r {z}]", "(t [u])", "{v}",
				"[a b]", " <", "> ", "[<]", "\n", "x"))
		case rng.Intn(6) == 0:
			b.WriteString(pick("[", "]", "{", "}", "(", ")", "=", "p{", "a b=", "-{", "+{"))
		default:
			b.WriteString(pick("text ", "w ", "q[r] ", "(y) ", "é "))
		}
	}
	return b.String()
}

func TestStructuredMatchesGoParser(t *testing.T) {
	parser := newParser(t)
	rng := rand.New(rand.NewSource(2))
	for n := 0; n < 50000; n++ {
		check(t, parser, randomDocument(rng, 4))
	}
}

// Editors reparse with the previous tree after each edit. On a valid document the result must
// equal a fresh parse; on an invalid one both must show an error, but tree-sitter does not
// promise the same error recovery.
func TestIncrementalParseMatchesFreshParse(t *testing.T) {
	parser := newParser(t)
	inserts := []string{"", "a", "<", "x[", "]", "p{a=b}[y]", "[", "{", "(", ")", "\n", "é", " <", "-[", "[amp]", "="}
	rng := rand.New(rand.NewSource(3))
	for n := 0; n < 20000; n++ {
		src := []byte(randomDocument(rng, 4))
		tree := parser.Parse(src, nil)
		for k := 0; k < 5; k++ {
			s := rng.Intn(len(src) + 1)
			e := s + rng.Intn(min(len(src)-s, 6)+1)
			ins := inserts[rng.Intn(len(inserts))]
			next := append(append(append([]byte{}, src[:s]...), ins...), src[e:]...)
			tree.Edit(&tree_sitter.InputEdit{
				StartByte: uint(s), OldEndByte: uint(e), NewEndByte: uint(s + len(ins)),
				StartPosition: point(src, s), OldEndPosition: point(src, e), NewEndPosition: point(next, s+len(ins)),
			})
			incremental := parser.Parse(next, tree)
			fresh := parser.Parse(next, nil)
			if incremental.RootNode().HasError() != fresh.RootNode().HasError() {
				t.Fatalf("edit [%d,%d) -> %q of %q: only one parse has an error", s, e, ins, src)
			}
			if a, b := incremental.RootNode().ToSexp(), fresh.RootNode().ToSexp(); a != b && !fresh.RootNode().HasError() {
				t.Fatalf("edit [%d,%d) -> %q of %q\nincremental %s\nfresh       %s", s, e, ins, src, a, b)
			}
			fresh.Close()
			tree.Close()
			tree, src = incremental, next
		}
		tree.Close()
	}
}

func point(src []byte, offset int) tree_sitter.Point {
	row := strings.Count(string(src[:offset]), "\n")
	return tree_sitter.Point{Row: uint(row), Column: uint(offset - (strings.LastIndex(string(src[:offset]), "\n") + 1))}
}

var corpusCase = regexp.MustCompile(`(?ms)^={3,}\n[^\n]*\n(?::[^\n]*\n)*={3,}\n(.*?)\n-{3,}\n`)

// The inputs of tree-sitter's own corpus tests must agree with the Go parser too.
func TestTreeSitterCorpusMatchesGoParser(t *testing.T) {
	parser := newParser(t)
	files, _ := filepath.Glob("../../test/corpus/*.txt")
	if len(files) == 0 {
		t.Fatal("no tree-sitter corpus found")
	}
	cases := 0
	for _, f := range files {
		data, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		// Each case is a name between lines of "=", the input, a line of "-", and the expected tree.
		for _, m := range corpusCase.FindAllStringSubmatch(string(data), -1) {
			check(t, parser, m[1])
			cases++
		}
	}
	if cases == 0 {
		t.Fatal("no cases in the tree-sitter corpus")
	}
}
