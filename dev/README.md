# Editor tooling

Tools for writing MinML in an editor. They are built from the repository root,
because they combine pieces from `dev/` and [`go`](../go).

| Tool | Contents |
| --- | --- |
| [`vscode/minml-preview`](vscode/minml-preview) | VS Code extension: live HTML preview and language support for MinML |
| [`neovim`](neovim) | Neovim plugin: file detection, language server, comment and bracket syntax |
| [`emacs`](emacs) | Emacs major mode `minml-mode`: file detection, language server through eglot, comments and brackets |
| [`sublime`](sublime) | Sublime Text package `LSP-minml`: syntax, comments, and the language server through the LSP package |
| [`zed`](zed) | Zed extension: grammar, highlighting, brackets, indentation, and the language server |
| [`helix`](helix) | Helix configuration: grammar, highlighting, indentation, and the language server |
| [`tree-sitter`](tree-sitter) | Tree-sitter grammar, equivalent to the Go parser, for Zed and Helix |
| [`jetbrains`](jetbrains) | JetBrains IDE plugin on [LSP4IJ](https://github.com/redhat-developer/lsp4ij): file type, language server, comments, and bracket matching |

## Build targets

Run these from the repository root.

| Command | Produces |
| --- | --- |
| `make vscode-live-preview` | The extension, installed into your local VS Code. Depends on `build-wasm` and `build-lsp`, so it also rebuilds the Go parser and language server. |
| `make neovim-plugin` | The language server, copied into `neovim/bin/`. |
| `make emacs-plugin` | The language server, copied into `emacs/bin/`. |
| `make sublime-plugin` | The language server, copied into `sublime/bin/`. |
| `make gen-parser` | The tree-sitter parser in `tree-sitter/src/`, from `tree-sitter/grammar.js` |
| `make test-grammar` | The tree-sitter corpus tests and the comparison with the Go parser |
| `make zed-extension` | `out/zed-extension`, the Zed extension with the grammar from this repository's current commit |
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

## Helix

Helix highlights with tree-sitter only; it does not use the server's semantic tokens.
Add the entries in [`helix/languages.toml`](helix/languages.toml) to
`~/.config/helix/languages.toml`, with the path to this repository, then:

```sh
mkdir -p ~/.config/helix/runtime/queries
ln -s "$PWD/dev/tree-sitter/queries" ~/.config/helix/runtime/queries/minml
hx --grammar build
```

Put `minml-lsp` on `PATH` (`make build-lsp` builds it), or set its full path as the
`command` of `language-server.minml-lsp`. Helix has no content check for `.m` files,
so only `.minml` is MinML.

Helix asks for completions whenever typing pauses inside a word, and it can turn
that off only for all languages at once, with `auto-completion = false` under
`[editor]` in `config.toml`. Completion stays available on request.

## Zed

Zed highlights with the tree-sitter grammar; its semantic-token support is off by
default. A Zed extension loads its grammar from a git commit, so commit the grammar
first, then run `make zed-extension` and install `out/zed-extension` with
**zed: install dev extension**. Zed needs Rust from rustup to build the extension.
`dev/zed/extension.toml` points at the `main` branch on GitHub; pin its `rev` to a
commit before publishing the extension.

The extension finds the server through the `lsp.minml-lsp.binary.path` setting,
then `MINML_LSP_PATH`, then `minml-lsp` on `PATH`. Zed extensions cannot bundle a
native binary.

Zed asks for completions while you type, and an extension cannot change that. To
open completion only on request, as in VS Code, add to Zed's `settings.json`:

```json
"languages": { "MinML": { "show_completions_on_input": false } }
```

**editor: toggle comments** (Cmd+/) handles line comments only; MinML comments are
block comments, so use **editor: toggle block comments**. Zed ends a comment at the
first `]` after its `-[`, not at the `]` that balances it, so it cannot uncomment text
that contains `]`: with a selection it adds a second comment, and with the cursor before
an inner `]` it removes that `]`. Undo, and delete the `-[ ` and ` ]` by hand.

## JetBrains IDEs

Run `make jetbrains-plugin`, then install the zip with **Settings | Plugins |
Install Plugin from Disk**. The plugin needs LSP4IJ from the Marketplace.

The plugin registers `.minml` only, because `.m` belongs to Objective-C in CLion
and other IDEs. To open `.m` files as MinML, add `*.m` to the MinML file type in
**Settings | Editor | File Types**. Completion opens automatically only after `{`,
as in VS Code, and otherwise on request.
