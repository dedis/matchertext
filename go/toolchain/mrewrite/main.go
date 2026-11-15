// Command mrewrite counts and rewrites Go string literals as matchertext literals
// M"(...)".
//
// Counting follows the analyzer (LLVM/src/RawRewrite.cpp), which decodes every
// escape. Before the rewrite, every backslash of an interpreted string literal
// "..." is a toothpick. After it, an escape keeps one toothpick if it decodes to a
// backslash (\\, \x5c, \u005c, ...) or to bytes that are not valid UTF-8 (\xff);
// every other escape, encodings (\x41, \u00e9, \101) and controls (\n, \a, \x00)
// included, loses its backslash. A literal whose source text or value is not
// matchertext keeps all its toothpicks.
//
// mrewrite writes M"(...)" only where the value stays the same: the literal has a
// backslash, its source text and value are matchertext, and its value is valid
// UTF-8 with no control character other than \n and \t and no BOM. The analyzer
// counts the other compliant literals as rewritten even though M"(...)" cannot
// hold their value; written, writtenBefore, and writtenAfter count only what
// mrewrite writes. Import paths and struct tags stay unchanged, because the
// compiler unquotes them directly. Directories named testdata or vendor are
// skipped, as are files that import "C".
//
// Usage: mrewrite [-n] dir...
//
// The rewritten code builds only with the patched toolchain (see ../build.sh).
// mrewrite prints one line of counts: interpreted literals, those with a
// backslash, their toothpicks before and after the rewrite as the analyzer counts
// them, and the literals written with their toothpicks before and after.
package main

import (
	"flag"
	"fmt"
	"go/ast"
	"go/parser"
	"go/token"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"unicode/utf8"

	"github.com/dedis/matchertext/go/matchertext"
)

type counts struct {
	files, changed        int
	literals, escaped     int
	toothpicks, converted int // all interpreted literals, as the analyzer counts them
	written               int
	before, after         int // toothpicks of the written literals
}

func main() {
	dryRun := flag.Bool("n", false, "count only; do not write files")
	flag.Parse()
	if flag.NArg() == 0 {
		fmt.Fprintln(os.Stderr, "usage: mrewrite [-n] dir...")
		os.Exit(2)
	}

	var c counts
	for _, root := range flag.Args() {
		err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if d.IsDir() {
				if name := d.Name(); path != root && (name == "testdata" || name == "vendor" || strings.HasPrefix(name, ".")) {
					return filepath.SkipDir
				}
				return nil
			}
			if !strings.HasSuffix(path, ".go") {
				return nil
			}
			return rewriteFile(path, *dryRun, &c)
		})
		if err != nil {
			fmt.Fprintln(os.Stderr, "mrewrite:", err)
			os.Exit(1)
		}
	}
	fmt.Printf("files=%d changed=%d literals=%d escaped=%d toothpicks=%d converted=%d written=%d writtenBefore=%d writtenAfter=%d\n",
		c.files, c.changed, c.literals, c.escaped, c.toothpicks, c.converted, c.written, c.before, c.after)
}

type edit struct {
	start, end int
	text       string
}

func rewriteFile(path string, dryRun bool, c *counts) error {
	src, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	fset := token.NewFileSet()
	file, err := parser.ParseFile(fset, path, src, parser.SkipObjectResolution)
	if err != nil {
		return nil // not valid Go for the stock parser: leave it unchanged
	}
	c.files++

	// Literals the compiler unquotes itself.
	keep := map[*ast.BasicLit]bool{}
	for _, imp := range file.Imports {
		if imp.Path.Value == `"C"` {
			return nil // cgo preamble and directives
		}
		keep[imp.Path] = true
	}
	ast.Inspect(file, func(n ast.Node) bool {
		if f, ok := n.(*ast.Field); ok && f.Tag != nil {
			keep[f.Tag] = true
		}
		return true
	})

	var edits []edit
	ast.Inspect(file, func(n ast.Node) bool {
		lit, ok := n.(*ast.BasicLit)
		if !ok || lit.Kind != token.STRING || keep[lit] || lit.Value[0] != '"' {
			return true
		}
		c.literals++
		body := lit.Value[1 : len(lit.Value)-1]
		if !strings.Contains(body, `\`) {
			return true
		}
		c.escaped++
		r, ok := decode(body)
		if !ok {
			return true // not valid Go: leave it
		}
		c.toothpicks += r.before
		c.converted += r.after
		if !r.writable {
			return true
		}
		c.written++
		c.before += r.before
		c.after += r.after
		start := fset.Position(lit.Pos()).Offset
		edits = append(edits, edit{start, start + len(lit.Value), `M"(` + r.value + `)"`})
		return true
	})
	if err != nil {
		return err
	}
	if len(edits) == 0 || dryRun {
		return nil
	}

	sort.Slice(edits, func(i, j int) bool { return edits[i].start < edits[j].start })
	var out strings.Builder
	out.Grow(len(src))
	at := 0
	for _, e := range edits {
		out.Write(src[at:e.start])
		out.WriteString(e.text)
		at = e.end
	}
	out.Write(src[at:])
	c.changed++
	info, err := os.Stat(path)
	if err != nil {
		return err
	}
	return os.WriteFile(path, []byte(out.String()), info.Mode().Perm())
}

// rewrite is an interpreted string literal under the analyzer's rewrite.
type rewrite struct {
	before, after int    // toothpicks in the source text and after the rewrite
	value         string // the literal's value
	writable      bool   // M"(value)" holds the value verbatim
}

// decode decodes the body of an interpreted string literal as the analyzer does,
// recording which escape produced each byte of the value. It reports false if the
// body is not valid Go.
func decode(body string) (rewrite, bool) {
	r := rewrite{before: strings.Count(body, `\`)}
	var value []byte
	var owner []int // per byte of value: the escape that produced it, or -1
	var kept []bool // per escape: it keeps its toothpick
	for s := body; s != ""; {
		if s[0] != '\\' {
			value = append(value, s[0])
			owner = append(owner, -1)
			s = s[1:]
			continue
		}
		v, multibyte, tail, err := strconv.UnquoteChar(s, '"')
		if err != nil {
			return r, false
		}
		id := len(kept)
		kept = append(kept, false)
		n := len(value)
		if c := s[1]; !multibyte && (c == 'x' || c >= '0' && c <= '7') {
			value = append(value, byte(v)) // \xHH and octal encode a byte
		} else {
			value = utf8.AppendRune(value, v)
		}
		for ; n < len(value); n++ {
			owner = append(owner, id)
		}
		s = tail
	}

	// An escape keeps its toothpick if it decodes to a backslash or to bytes
	// that are not valid UTF-8, which a raw literal cannot hold.
	r.writable = true
	for i := 0; i < len(value); {
		c, size := utf8.DecodeRune(value[i:])
		if c == utf8.RuneError && size == 1 {
			r.writable = false
			if owner[i] >= 0 {
				kept[owner[i]] = true
			}
		} else if c == '\\' && owner[i] >= 0 {
			kept[owner[i]] = true
		} else if c == 0xFEFF || c != '\n' && c != '\t' && (c < 0x20 || c >= 0x7F && c <= 0x9F) {
			r.writable = false // the compiler rejects NUL and BOM; other controls would be invisible
		}
		i += size
	}
	r.value = string(value)

	if !isMatchertext(body) || !isMatchertext(r.value) {
		r.after = r.before
		r.writable = false
		return r, true
	}
	for _, k := range kept {
		if k {
			r.after++
		}
	}
	return r, true
}

// isMatchertext reports whether the matchers (), [], {} of s match.
func isMatchertext(s string) bool {
	unmatched, err := matchertext.UnmatchedOffsets(strings.NewReader(s))
	return err == nil && len(unmatched) == 0
}
