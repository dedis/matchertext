# Matchertext Developer Tools

This directory contains various developer tools and extensions for working with matchertext and MinML.

## Available Tools

### [minml-preview](minml-preview)
A VS Code extension that provides a live preview for MinML files (`.minml`, `.m`). It uses a WebAssembly version of the Go parser to render MinML to HTML in real-time.

## Installation

Most tools in this directory can be installed or built using the root `Makefile`. For example, to install the VS Code extension locally:

```bash
make vscode-live-preview
```
