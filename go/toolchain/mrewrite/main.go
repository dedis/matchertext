// Command mrewrite counts and rewrites Go string literals as matchertext literals
// M"(...)".
//
// Counting follows the analyzer (LLVM/src/RawRewrite.cpp). In an interpreted
// string literal "...", \\ leaves one toothpick; \n, \t, \", and \' leave none; the
// control escapes \a, \b, \f, \r, \v keep their toothpick; and the encoding escapes
// (\xHH, \u and \U code points, octal) stay as written and are not counted, before or
// after. A literal whose source text is not matchertext keeps all its toothpicks.
//
// mrewrite writes M"(...)" only where the value stays the same: the literal has
// a backslash, no encoding or control escape, and the matchers (), [], {} of its
// value match. Import paths and struct tags stay unchanged, because the compiler
// unquotes them directly. Directories named testdata or vendor are skipped, as
// are files that import "C".
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
		e := scanEscapes(body)
		before := e.backslashes - e.encodings
		c.toothpicks += before
		compliant, scanErr := isMatchertext(body)
		if scanErr != nil {
			err = scanErr
			return false
		}
		if !compliant {
			c.converted += before
			return true
		}
		c.converted += e.kept
		if e.encodings > 0 || e.controls > 0 {
			return true
		}
		value, unquoteErr := strconv.Unquote(lit.Value)
		if unquoteErr != nil {
			return true // not valid Go: leave it
		}
		if balanced, scanErr := isMatchertext(value); scanErr != nil || !balanced {
			err = scanErr
			return scanErr == nil
		}
		c.written++
		c.before += before
		c.after += strings.Count(value, `\`)
		start := fset.Position(lit.Pos()).Offset
		edits = append(edits, edit{start, start + len(lit.Value), `M"(` + value + `)"`})
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

// escapes describes the escapes of an interpreted string literal's body.
type escapes struct {
	backslashes int // all backslashes
	encodings   int // escapes that encode a character by number: \x, \u, \U, octal
	controls    int // \a, \b, \f, \r, \v: characters a raw literal cannot hold
	kept        int // toothpicks after the rewrite: one per \\ and per control escape
}

func scanEscapes(body string) escapes {
	var e escapes
	for i := 0; i < len(body); i++ {
		if body[i] != '\\' {
			continue
		}
		e.backslashes++
		if i+1 == len(body) {
			break
		}
		switch c := body[i+1]; {
		case c == 'x':
			e.encodings++
			i += 3
		case c == 'u':
			e.encodings++
			i += 5
		case c == 'U':
			e.encodings++
			i += 9
		case c >= '0' && c <= '7':
			e.encodings++
			i += 3
		case strings.IndexByte("abfrv", c) >= 0:
			e.controls++
			e.kept++
			i++
		case c == '\\':
			e.backslashes++
			e.kept++
			i++
		default: // \n, \t, \", \'
			i++
		}
	}
	return e
}

// isMatchertext reports whether the matchers (), [], {} of s match.
func isMatchertext(s string) (bool, error) {
	unmatched, err := matchertext.UnmatchedOffsets(strings.NewReader(s))
	return err == nil && len(unmatched) == 0, err
}
