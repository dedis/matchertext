This repository contains a draft paper and experimental code
related to matchertext, a syntactic discipline that allows
strings in one compliant language to be embedded verbatim without escaping
(e.g., via cut-and-paste) into itself or another compliant language.

For an overview of the matchertext idea please see
[the matchertext paper](https://bford.info/pub/lang/matchertext/).

The main contents of this repository are currently:

* [doc](doc): the LaTeX source for the in-progress matchertext paper.
* [go](go): experimental Go code for parsing and converting matchertext.
* [devtools](devtools): developer tools and extensions (e.g., VS Code preview).

### Build

In order to build the MinML cli tool, you can run Makefile. Below is an overview of the various build commands and what they do:

| Command               | Description                                                                                        |
|-----------------------|----------------------------------------------------------------------------------------------------|
| build                 | Build the CLI tool with various options                                                            |
| build-wasm            | Builds the WASM binary used by browser-based tools and the VS Code extension.                      |
| vscode-live-preview   | Builds the WASM and extension, then installs it to your local VS Code extensions directory.       |
