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

`build` is a wrapper over the native command, so this package builds on its own
just as well:

```sh
go build -o minml ./go/markup/minml/cmd/
```

The other two cannot: `build-lsp` copies Tree-sitter queries out of `dev/`
before compiling, and `build-wasm` writes its output into the VS Code
extension under `dev/` as well as `out/wasm/`.

`make build-lsp` copies the Tree-sitter highlight queries into the package
before building, because the server embeds them at build time. Building by hand
means copying them first:

```sh
cp -R dev/tree-sitter/queries go/markup/minml/lsp/queries
go build -o minml-lsp ./go/markup/minml/cmd/lsp/
```

## Language server

`minml-lsp` serves `.m` and `.minml` files. It parses with Tree-sitter, so it
tolerates errors and keeps working on incomplete documents.

- **Diagnostics** for syntax errors, such as unmatched brackets
- **Completion** for HTML5 tags and attributes, chosen by context
- **Hover** documentation for HTML5 tags and every MinML construct

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

- Type `div[unclosed` and a "Missing ]" diagnostic should appear
- Type `[` for tag completions or `{` for attribute completions
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
