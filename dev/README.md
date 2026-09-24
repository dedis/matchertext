# Editor tooling

Tools for writing MinML in an editor. They are built from the repository root,
because they combine pieces from `dev/` and [`go`](../go).

| Tool | Contents |
| --- | --- |
| [`vscode/minml-preview`](vscode/minml-preview) | VS Code extension: live HTML preview and language support for MinML |

## Build targets

Run these from the repository root.

| Command | Produces |
| --- | --- |
| `make vscode-live-preview` | The extension, installed into your local VS Code. Depends on `build-wasm` and `build-lsp`, so it also rebuilds the Go parser and language server. |

`make vscode-live-preview` needs Node and npm, and it removes any stale
`minml-preview` installation before copying the new one in.

The extension's highlighting, diagnostics, hover, and completion come from the
`minml-lsp` language server, which parses with the converter's own parser.
