# Matchertext

Matchertext is a syntactic discipline that lets a string in one language be
embedded in another **verbatim**, with no escaping, obfuscation, or expansion.
Cut and paste becomes correct by construction.

It is one rule: **matchers must match.** The ASCII matcher pairs `()`, `[]` and
`{}` must appear in properly nested pairs throughout a compliant string.
Everything else is a nonmatcher and is unconstrained.

```
matchertext:      a(b)c      call(item[index])      ' OR '1'='1
not matchertext:  a)b        ([)]                   smile :]
```

The payoff is a boundary that does not depend on the contents. A host language
that delimits a hole with a matcher pair can find the end of an embedded value
by counting balance, so no character inside the value needs escaping and no
character can end the value early.

For the idea in full, see
[the matchertext paper](https://bford.info/pub/lang/matchertext/).

## Two papers live here

| Paper                                           | Source                                     | Subject                                                                                                                    |
|-------------------------------------------------|--------------------------------------------|----------------------------------------------------------------------------------------------------------------------------|
| Towards Verbatim Interlanguage Embedding        | [`doc`](doc)                               | The discipline itself: definition, host-language extensions, MinML, and a compliance study of real-world code              |
| Structural Injection Resistance by Construction | [`injection-research`](injection-research) | What the discipline is worth against injection: a security property, a SQLite implementation, and a CVE corpus measurement |

## Directory map

Each directory documents itself; follow the link rather than looking for
instructions here.

| Directory                                  | Contents                                                                             |
|--------------------------------------------|--------------------------------------------------------------------------------------|
| [`doc`](doc)                               | LaTeX source for the matchertext paper                                               |
| [`lean`](lean)                             | Machine-checked proofs of the properties both papers rely on                         |
| [`go`](go)                                 | Go reference implementation: the matchertext scanner, MinML, and the language server |
| [`dev`](dev)                               | Editor tooling: a Tree-sitter grammar and a VS Code extension                        |
| [`LLVM`](LLVM)                             | Clang-based tool that measures matchertext compliance across source trees            |
| [`injection-research`](injection-research) | The injection study: CVE pipeline, corpus, and a matchertext-aware SQLite            |

Smaller items with no README of their own: `perl/escapes.pl` and
`raku/escapes.raku` sketch matcher escape sequences for those languages, and
`test/index.m` is a sample MinML document.

## Building

The MinML tooling is built from the repository root, which is a single entry
point rather than the home of the work. Three of these targets have no other
home: they read from one directory and write into another, and
`vscode-live-preview` needs the `minml-lsp` binary left at the root.

| Command                    | Produces                                              |
|----------------------------|-------------------------------------------------------|
| `make build`               | the `minml` CLI                                       |
| `make build-lsp`           | the `minml-lsp` language server                       |
| `make build-wasm`          | the WebAssembly parser, for the browser and VS Code   |
| `make gen-parser`          | the Tree-sitter C parser from `grammar.js`            |
| `make vscode-live-preview` | the VS Code extension, installed locally              |

`build` and `gen-parser` are one-line wrappers over the native command for
their directory; the linked README gives that command directly. Everything
else in the repository builds from its own directory.

## Contributing

This is a research repository and a work in progress. Contributions are
welcome; see the paper for the open questions each part is trying to answer.
