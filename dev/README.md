# Editor tooling

Tools for writing MinML in an editor. They are built from the repository root,
because they combine pieces from `dev/` and [`go`](../go).

| Tool | Contents |
| --- | --- |
| [`vscode/minml-preview`](vscode/minml-preview) | VS Code extension: live HTML preview and language support for MinML |
| [`neovim`](neovim) | Neovim plugin: file detection, language server, comment and bracket syntax |
| [`emacs`](emacs) | Emacs major mode `minml-mode`: file detection, language server through eglot, comments and brackets |
| [`sublime`](sublime) | Sublime Text package `LSP-minml`: syntax, comments, and the language server through the LSP package |
| [`jetbrains`](jetbrains) | JetBrains IDE plugin on [LSP4IJ](https://github.com/redhat-developer/lsp4ij): file type, language server, comments, and bracket matching |

## Build targets

Run these from the repository root.

| Command | Produces |
| --- | --- |
| `make vscode-live-preview` | The extension, installed into your local VS Code. Depends on `build-wasm` and `build-lsp`, so it also rebuilds the Go parser and language server. |
| `make neovim-plugin` | The language server, copied into `neovim/bin/`. |
| `make emacs-plugin` | The language server, copied into `emacs/bin/`. |
| `make sublime-plugin` | The language server, copied into `sublime/bin/`. |
| `make jetbrains-plugin` | `jetbrains/build/distributions/minml-jetbrains-*.zip`, with the language server inside. Needs JDK 21 in `JAVA_HOME`; every JetBrains IDE bundles one, for example `/Applications/GoLand.app/Contents/jbr/Contents/Home`. |

`make vscode-live-preview` needs Node and npm, and it removes any stale
`minml-preview` installation before copying the new one in.

In every editor, highlighting, diagnostics, hover, and completion come from the
`minml-lsp` language server, which parses with the converter's own parser.
The server sends no tokens for brackets; each editor colors them itself.

Each plugin looks for the server in the same order: `MINML_LSP_PATH`, then the
binary bundled with the plugin, then `minml-lsp` on `PATH`. The VS Code extension's
`minml.lspPath` setting and Emacs's `minml-lsp-path` option come before all three.

## Neovim

Needs Neovim 0.11 or later. Run `make neovim-plugin`, then add `dev/neovim` to the
runtime path, for example with lazy.nvim:

```lua
{ dir = "/path/to/matchertext/dev/neovim" }
```

`.minml` files are MinML. A `.m` file is MinML when its first non-blank line starts
with an element or a MinML construct; otherwise Neovim's own `.m` detection
(Objective-C, MATLAB, and others) decides. Completion opens only on request, as
in VS Code.

## Emacs

Needs Emacs 29 or later; semantic highlighting needs Emacs 31, whose eglot has
`eglot-semantic-tokens-mode`. Run `make emacs-plugin`, then:

```elisp
(add-to-list 'load-path "/path/to/matchertext/dev/emacs")
(require 'minml-mode)
```

`minml-mode` starts eglot itself; set `minml-start-eglot` to nil to start it by
hand. `.m` files are detected as in Neovim.

## Sublime Text

Needs the LSP package from Package Control. Run `make sublime-plugin`, then link
the package into Sublime's `Packages` directory under the name `LSP-minml`, for
example on macOS:

```sh
ln -s "$PWD/dev/sublime" ~/Library/"Application Support/Sublime Text/Packages/LSP-minml"
```

Install it as a directory, not a `.sublime-package` archive, so the bundled server
can run. The LSP package turns semantic highlighting off by default; set
`"semantic_highlighting": true` in **Preferences | Package Settings | LSP |
Settings** to get the server's colors. Without it, only brackets are colored.
The package claims `.minml` only; for `.m` files use **View | Syntax | Open all
with current extension as | MinML**.

## JetBrains IDEs

Run `make jetbrains-plugin`, then install the zip with **Settings | Plugins |
Install Plugin from Disk**. The plugin needs LSP4IJ from the Marketplace.

The plugin registers `.minml` only, because `.m` belongs to Objective-C in CLion
and other IDEs. To open `.m` files as MinML, add `*.m` to the MinML file type in
**Settings | Editor | File Types**. Completion opens automatically only after `{`,
as in VS Code, and otherwise on request.
