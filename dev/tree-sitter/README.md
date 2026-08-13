# tree-sitter-minml

A [tree-sitter](https://tree-sitter.github.io/tree-sitter/) grammar for [MinML](https://bford.info/matchertext/), the matchertext-based markup language.

## What is MinML?

MinML is a minimal markup language built on the *matchertext* principle: brackets always come in matched pairs, so any well-formed MinML document is automatically self-delimiting. The key construct is the **element**:

```
tag{attributes}[content]
```

A document is a flat sequence of nodes — elements, character references, comments, raw blocks, and plain text.

| Syntax | Description |
|---|---|
| `em[hello]` | Element with tag `em` and content `hello` |
| `img{src=cat.jpg alt=[a cat]}[]` | Element with attributes |
| `[reg]` / `[#174]` / `[#xAE]` | Character references (named / decimal / hex) |
| `"[verbatim]` | Quoted string — brackets not interpreted |
| `+[<b>raw</b>]` | Raw block — content passed through as-is |
| `-[a comment]` | Comment |
| `?[xml version=1.0]` | Processing instruction |
| `<em[hi]>` | Space-sucker markers — tell processors to trim adjacent whitespace |
| `[[<]]` `[[>]]` `[(<)]` `[(>)]` | Matcher escapes for literal bracket characters |

## Prerequisites

- [tree-sitter CLI](https://tree-sitter.github.io/tree-sitter/creating-parsers#installation) ≥ 0.24
- Go ≥ 1.22 (for the Go binding)

Install the CLI:

```sh
npm install -g tree-sitter-cli
# or
cargo install tree-sitter-cli
```

## Generating the parser

The generated `src/` files are **not** committed (they are gitignored). You must generate them before doing anything else:

```sh
cd dev/tree-sitter
tree-sitter generate
```

This reads `grammar.js` and writes `src/parser.c` plus the supporting `src/tree_sitter/` headers.

The repository root offers the same thing as a make target, which is what the
other build steps expect:

```sh
make gen-parser
```

## Running the test suite

The corpus test file at `test/corpus/minml.txt` contains 39 test cases covering all node types, disambiguation edge cases, and error recovery.

```sh
cd dev/tree-sitter
tree-sitter test
```

Expected output:

```
Total parses: 39; successful parses: 39; failed parses: 0; success percentage: 100.00%
```

## Parsing files

Parse any `.m` or `.minml` file and inspect the syntax tree:

```sh
# pretty-print the syntax tree for the full example document
tree-sitter parse examples/12-full-document.m

# check all example files at once — none should contain ERROR or MISSING nodes
tree-sitter parse examples/*.m
```

Example tree for `em[hello]`:

```
(source_file [0, 0] - [0, 9]
  (element [0, 0] - [0, 9]
    tag: (tag_name [0, 0] - [0, 2])
    content: (content_block [0, 2] - [0, 9]
      (word [0, 3] - [0, 8]))))
```

You can also parse snippets inline via `/dev/stdin`:

```sh
# basic element
echo 'em[hello]' | tree-sitter parse /dev/stdin

# element with attributes
echo 'img{src=cat.jpg alt=[a photo]}[]' | tree-sitter parse /dev/stdin

# nested brackets inside a comment (balanced — not a parse error)
echo '-[see [RFC 1234]]' | tree-sitter parse /dev/stdin

# raw block containing HTML with brackets in the content
echo '+[<html>[body]</html>]' | tree-sitter parse /dev/stdin

# literal angle brackets in text content
echo 'p[2 < 3 and x > 0]' | tree-sitter parse /dev/stdin

# spaces around '=' in an attribute — should produce ERROR nodes
echo 'img{src = cat.jpg}[]' | tree-sitter parse /dev/stdin
```

## Syntax highlighting

The `queries/highlights.scm` file maps node types to standard capture names.

| Capture | Node types | Typical colour |
|---|---|---|
| `@tag` | `tag_name` — element tag identifiers | blue |
| `@tag.attribute` | `attr_name` — attribute names | teal |
| `@punctuation.bracket` | `[` `]` `{` `}` — structural delimiters | grey |
| `@constant.builtin` | `named_ref`, `decimal_ref`, `hex_ref` — character references | green |
| `@string` | `quoted_string` | green |
| `@string.special` | `raw_block` | green |
| `@comment` | `comment` | grey italic |
| `@keyword.directive` | `processing_instruction` | blue |
| `@punctuation.delimiter` | `<` `>` — space-suckers | grey |
| `@string.escape` | `matcher_escape` | green |

Run highlighting in the terminal:

```sh
tree-sitter highlight examples/12-full-document.m
```

A successful run produces coloured terminal output with **no warning lines**.

## Testing and Verifying Queries

### 1. Manual Verification

The fastest way to verify changes to your queries is using the Tree-sitter CLI.

#### Syntax Highlighting

Run the `highlight` command on a sample MinML file or an example:

```sh
tree-sitter highlight examples/12-full-document.m
```

**Check for errors:**
If you see a warning like `Query error at 12:3. Invalid node type tag_name`, it means your `.scm` file references a node type that doesn't exist in `grammar.js`. Always check the output for such warnings.

**Verify captures:**
You can see which nodes are being captured by which names using the `query` command:

```sh
tree-sitter query queries/highlights.scm examples/02-basic-element.m
```

#### Indentation

Verify indentation captures using the `query` command:

```sh
tree-sitter query queries/indents.scm examples/04-nested-elements.m
```

You should see `@indent.begin` on every `[` and `@indent.end` on every `]`.

### 2. Automated Validation

While highlight assertions in the corpus are difficult for MinML (because `;` is valid text content and not ignored by the grammar), you can still perform automated validation of the query files themselves.

#### Query Linting

The `highlight` command has a `--check` flag that validates the query file against the grammar without needing a source file:

```sh
tree-sitter highlight --check
```

This is useful for CI to catch typos in node names or capture names.

## Using the Go binding

The Go binding wraps the C parser via cgo. Add it as a module dependency:

```sh
go get github.com/dedis/matchertext/dev/tree-sitter/bindings/go
go get github.com/tree-sitter/go-tree-sitter
```

Basic usage:

```go
package main

import (
    "context"
    "fmt"

    tree_sitter "github.com/tree-sitter/go-tree-sitter"
    tree_sitter_minml "github.com/dedis/matchertext/dev/tree-sitter/bindings/go"
)

func main() {
    parser := tree_sitter.NewParser()
    defer parser.Close()

    language := tree_sitter.NewLanguage(tree_sitter_minml.Language())
    if err := parser.SetLanguage(language); err != nil {
        panic(err)
    }

    src := []byte(`p[Hello em[world]]`)
    tree := parser.Parse(src, nil)
    defer tree.Close()

    fmt.Println(tree.RootNode())
}
```

Run the binding's own test:

```sh
cd dev/tree-sitter
go test ./bindings/go/
```

## Repository layout

```
dev/tree-sitter/
├── grammar.js              # Grammar definition (source of truth)
├── tree-sitter.json        # Tree-sitter project metadata
├── go.mod / go.sum         # Go module (only binding kept)
├── bindings/
│   └── go/
│       ├── binding.go      # cgo wrapper — exposes Language() unsafe.Pointer
│       └── binding_test.go # Smoke test: loads the grammar
├── queries/
│   ├── highlights.scm      # Syntax-highlight capture names
│   └── indents.scm         # Indentation rules
├── test/
│   └── corpus/
│       └── minml.txt       # 39 corpus tests (tree-sitter test format)
└── examples/               # Sample .m files for manual parsing/highlight checks
    ├── 01-text-only.m
    ├── 02-basic-element.m
    ├── ...
    └── 12-full-document.m
```

`src/` is generated by `tree-sitter generate` and is gitignored.

## Full verification checklist

Run all of the following from `dev/tree-sitter/`. Start by regenerating the parser:

```sh
tree-sitter generate
```

Then run each check in order:

```sh
# 1. Corpus test suite — should report 39/39
tree-sitter test

# 2. Parse all examples — zero ERROR or MISSING nodes
tree-sitter parse examples/*.m

# 3. Syntax highlighting — coloured output, no warnings printed
tree-sitter highlight examples/12-full-document.m

# 4. Go binding smoke test
go test ./bindings/go/
```

## Design notes

**No global `extras`.**  Tree-sitter grammars typically declare `extras: $ => [/\s/]` so that whitespace is silently accepted anywhere. This grammar sets `extras: $ => []` to explicitly disable that behaviour because MinML whitespace is *meaningful*: inside a `content_block` it is captured as a `text` node (preserving it for renderers), and inside an `attr_block` it is the only separator between attributes. Without extras, `img{src = cat.jpg}[]` (spaces around `=`) is correctly rejected. Whitespace is handled explicitly: `text` matches `/[^\[\]{}a-zA-Z_+\-?"]+/` (spaces, digits, punctuation, and literal `<`/`>` in content), and `attr_block` uses `optional(/\s+/)` between attributes.

**Angle brackets in content.**  Literal `<` and `>` are allowed in text content (e.g. `p[2 < 3]`). The `element` rule carries `prec(1)` so that `<tag[...]>` is still parsed as a space-sucker element rather than text.

**Balanced brackets in verbatim constructs.**  `comment`, `raw_block`, `quoted_string`, and `processing_instruction` all use the `_raw_content` helper rule, which allows arbitrarily nested `[...]` pairs inside these constructs instead of stopping at the first `]`. For example, `-[see [RFC 1234]]` is a single comment.

**`char_ref` vs `content_block` disambiguation.**  Both begin with `[`. A `content_block` only appears as a named child of `element` or as an `attr_value`; it is never placed in `_node`. A bare `[...]` at the top level or inside another `content_block` is therefore always a `char_ref`.

**Go-only binding.**  The tree-sitter scaffolding generates bindings for C, Rust, Node.js, Python, Swift, and Zig. This project retains only the Go binding because the target use case is a Language Server Protocol implementation written in Go. All other generated binding files have been removed to reduce maintenance surface.
