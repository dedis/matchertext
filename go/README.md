# Go implementation

The reference implementation of the matchertext scanner, the MinML markup
language built on it, and the tooling around both.

## Packages

| Package                | Contents                                                                                                        |
|------------------------|-----------------------------------------------------------------------------------------------------------------|
| `matchertext`          | The discipline itself: a parser, the syntax definitions, and `UnmatchedOffsets` for locating unmatched matchers |
| `markup/ast`           | Shared document tree for the markup languages                                                                   |
| `markup/minml`         | MinML reader and writer                                                                                         |
| `markup/html`          | HTML reader and writer                                                                                          |
| `markup/xml`           | XML reader and writer                                                                                           |
| `markup/minml/cmd`     | The `minml` CLI                                                                                                 |
| `markup/minml/lsp`     | Language server implementation                                                                                  |
| `markup/minml/cmd/lsp` | The `minml-lsp` binary                                                                                          |
| `wasm`                 | WebAssembly entry point for the browser tools and the VS Code extension                                         |

`matchertext` is used as an independent oracle by the injection study: the C
scanner in `../injection-research/sqlite` is differentially tested against it,
so the two implementations share no code by design.

## Build

From the repository root:

```sh
make build       # the minml CLI
make build-lsp   # the language server
make build-wasm  # the WebAssembly binary
```

`build` and `build-lsp` are wrappers over the native commands, so both build
on their own just as well:

```sh
go build -o minml ./go/markup/minml/cmd/
go build -o minml-lsp ./go/markup/minml/cmd/lsp/
```

`build-wasm` cannot: it writes its output into the VS Code extension under
`dev/` as well as `out/wasm/`.

## Language server

`minml-lsp` serves `.m` and `.minml` files. It parses with the same parser as
the converter, so its diagnostics are exactly the documents the converter
rejects. The parser recovers from each syntax error, so the server keeps
working on incomplete documents. The editor sends only the changed text, and
the server reparses only the elements around each edit.

- **Diagnostics** for every syntax error, such as unmatched brackets
- **Completion** for HTML5 tags, and for attributes inside `{...}`
- **Hover** documentation for HTML5 tags, character references, and MinML constructs
- **Semantic highlighting** of element and attribute names, references,
  comments, and raw text. Brackets get no tokens; each editor colors them itself

### Running it by hand

The protocol needs an `initialize` request and a `Content-Length` header on
every message, so the simplest manual test is a piped string:

```sh
printf 'Content-Length: 58\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./minml-lsp
```

Exercising a feature means sending `initialize` first, then the action:

```sh
(
  printf 'Content-Length: 58\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
  sleep 0.1
  printf 'Content-Length: 134\r\n\r\n{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///test.m","version":1,"text":"div[unclosed"}}}'
) | ./minml-lsp
```

`Content-Length` must be the exact byte count of the JSON body. Check it with
`echo -n '<json>' | wc -c`; an off-by-one hangs the server rather than
reporting an error.

### Running it in VS Code

```sh
make vscode-live-preview
```

installs the extension and the server together. Then open a `.m` file:

- Type `div[unclosed` and an "unmatched opener [" diagnostic should appear
- Press Ctrl+Space for tag completions, or type `{` after a tag name for
  attribute completions. MinML is prose, so completions do not open while typing
- Hover a tag such as `div`, or a construct such as `-[a comment]`

If nothing appears, open the **Output** panel and select **MinML Language
Server** to read its log.

### Debugging

| Where | How |
| --- | --- |
| CLI | `./minml-lsp --debug` |
| TCP, for external inspection tools | `./minml-lsp --addr :2087` |
| VS Code | Settings, search `Minml: Debug` |

### Testing tools

| Tool | Use |
| --- | --- |
| [LSP Devtools](https://github.com/swyddfa/lsp-devtools) | TUI showing live JSON-RPC traffic |
| [LSP Inspector](https://microsoft.github.io/language-server-protocol/inspector/) | Graphical timeline from a server log |
| [pytest-lsp](https://github.com/swyddfa/pytest-lsp) | Automated end-to-end tests against the binary |
| [VS Code Extension Tester](https://github.com/redhat-developer/vscode-extension-tester) | Drives a real VS Code instance |

The server is built on `glsp`, so in-process tests can connect a mock client
over `net.Pipe()` and skip stdio and TCP entirely.

## Tests

```sh
go test ./go/...
```
