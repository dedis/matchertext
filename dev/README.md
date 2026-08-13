# Editor tooling

Tools for writing MinML in an editor. Both are built from the repository root,
because they combine pieces from `dev/` and [`go`](../go).

| Tool | Contents |
| --- | --- |
| [`tree-sitter`](tree-sitter) | Tree-sitter grammar for MinML, with highlight and indent queries and a Go binding |
| [`vscode/minml-preview`](vscode/minml-preview) | VS Code extension giving a live HTML preview of a MinML document |

## Build targets

Run these from the repository root.

| Command | Produces |
| --- | --- |
| `make gen-parser` | The Tree-sitter C parser, from `tree-sitter/grammar.js`. The generated `src/` is gitignored, so this is the first step in a fresh checkout. |
| `make vscode-live-preview` | The extension, installed into your local VS Code. Depends on `build-wasm` and `build-lsp`, so it also rebuilds the Go parser and language server. |

`make vscode-live-preview` needs Node and npm, and it removes any stale MinML
extension already installed before copying the new one in.

The queries under `tree-sitter/queries/` are consumed twice: the extension reads
them for highlighting, and `make build-lsp` copies them into the language server,
which embeds them at build time. Editing a query means rebuilding the server, not
just reloading the editor.
