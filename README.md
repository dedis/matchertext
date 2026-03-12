This repository contains a draft paper and experimental code
related to matchertext, a syntactic discipline that allows
strings in one compliant language to be embedded verbatim without escaping
(e.g., via cut-and-paste) into itself or another compliant language.

For an overview of the matchertext idea please see
[the matchertext paper](https://bford.info/pub/lang/matchertext/).

The main contents of this repository are currently:

* [doc](doc): the LaTeX source for the in-progress matchertext paper.
* [go](go): experimental Go code for parsing and converting matchertext.

### Build

In order to build the MinML cli tool, you can run Makefile. Below is an overview of the various build commands and what they do:

| Command    | Description                                                                                        |
|------------|----------------------------------------------------------------------------------------------------|
| build      | Build the CLI tool with various options                                                            |
| build-wasm | In order to use different functions in a browser environment, this command builds the wasm binary. |
