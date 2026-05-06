This repository contains a draft paper and experimental code
related to matchertext, a syntactic discipline that allows
strings in one compliant language to be embedded verbatim without escaping
(e.g., via cut-and-paste) into itself or another compliant language.

For an overview of the matchertext idea please see
[the matchertext paper](https://bford.info/pub/lang/matchertext/).

The main contents of this repository are currently:

* [doc](doc): the LaTeX source for the in-progress matchertext paper.
* [go](go): experimental Go code for parsing and converting matchertext.
* [dev](dev): developer tools, including a Tree-sitter grammar and VS Code extension.

### Build

In order to build the MinML tools, you can use the `Makefile`. Below is an overview of the various build commands:

| Command               | Description                                                                                        |
|-----------------------|----------------------------------------------------------------------------------------------------|
| build                 | Build the main `minml` CLI tool.                                                                   |
| build-lsp             | Build the `minml-lsp` Language Server.                                                             |
| build-wasm            | Builds the WASM binary used by browser-based tools and the VS Code extension.                      |
| vscode-live-preview   | Builds the WASM and extension, then installs it to your local VS Code extensions directory.       |
| gen-parser            | Generates the Tree-sitter C parser from `grammar.js`.                                              |

### Developer Tools

#### MinML Language Server (`minml-lsp`)
An LSP server written in Go that provides real-time feedback and intelligent editing features for MinML files (`.m`, `.minml`). It uses Tree-sitter for error-tolerant parsing.
- **Diagnostics**: Immediate feedback on syntax errors, such as unmatched brackets.
- **Completion**: Intelligent suggestions for HTML5 tags and attributes based on context.
- **Hover**: Documentation for HTML5 tags and all MinML constructs (character references, raw blocks, comments, etc.).

#### Testing the LSP Server

##### 1. Manual Testing (CLI)
The LSP protocol requires an initialization step and specific headers (`Content-Length`) for every message. The easiest way to test it manually is to pipe a formatted string to the server.

**Test Initialization:**
Run this command to see the server's capabilities:
```bash
printf "Content-Length: 58\r\n\r\n{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}" | ./minml-lsp
```

**Test Diagnostics:**
To test features like diagnostics, you must send an `initialize` request followed by the action. You can use a subshell to send multiple messages:
```bash
(
  printf "Content-Length: 58\r\n\r\n{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}"
  sleep 0.1
  printf "Content-Length: 134\r\n\r\n{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file:///test.m\",\"version\":1,\"text\":\"div[unclosed\"}}}"
) | ./minml-lsp
```
*Note: The `Content-Length` must be exactly the number of bytes in the JSON body — use `echo -n '<json>' | wc -c` to verify.*

##### 2. Testing in VS Code
1. Install the extension and LSP binary together: `make vscode-live-preview`
   The LSP binary is bundled inside the extension directory automatically.
2. Open a `.m` file in VS Code.
3. **Diagnostics**: Type `div[unclosed`. You should see a "Missing ]" error in the Problems tab.
4. **Completion**: Type `[` to see tag suggestions or `{` to see attribute suggestions.
5. **Hover**: Hover over a tag like `div` to see its HTML5 documentation, or hover over `-[a comment]` to see MinML construct documentation.

If features don't appear, check the **Output** panel and select **MinML Language Server** from the dropdown to view logs.

#### Debugging the LSP
You can enable detailed debug logging to troubleshoot the server:

- **CLI**: Run with the `--debug` flag: `./minml-lsp --debug`
- **TCP mode** (useful with external inspection tools): `./minml-lsp --addr :2087`
- **VS Code**: Go to Settings (`Cmd+,`), search for `Minml: Debug`, and check the box. Logs will appear in the **Output** panel.

### Recommended Testing Tools

For more advanced testing and protocol validation, the following tools are highly recommended:

| Tool | Purpose | Key Feature |
|---|---|---|
| **[LSP Devtools](https://github.com/swyddfa/lsp-devtools)** | Inspection | A TUI that shows live JSON-RPC traffic between editor and server. |
| **[LSP Inspector](https://microsoft.github.io/language-server-protocol/inspector/)** | Visualization | Upload server logs to see a graphical timeline of requests and responses. |
| **[pytest-lsp](https://github.com/swyddfa/pytest-lsp)** | E2E Testing | A Python-based framework to write automated tests that run the Go binary. |
| **[VS Code Extension Tester](https://github.com/redhat-developer/vscode-extension-tester)** | Integration | Automates a real VS Code instance to test UI and LSP features together. |

#### Pro Tip: In-Memory Testing in Go
Since the server uses `glsp`, you can write unit tests in Go using `net.Pipe()`. This allows you to connect a mock client directly to your server handler in memory, avoiding the need for actual `stdio` or TCP during testing.
