# MinML Live Preview

A VS Code extension that provides real-time HTML preview for MinML files (`.minml`, `.m`).

## Features

- **Live Preview:** Renders your MinML markup to HTML as you type.
- **WASM-Powered:** Uses the project's Go parser compiled to WebAssembly for accurate and fast conversion.
- **Local Asset Support:** Automatically resolves relative image paths (e.g., `img{src=my-image.png}[]`) using the document's directory.
- **VS Code Theme Integration:** Uses default VS Code styles for consistent appearance.

## Usage

1. Open a `.minml` or `.m` file in VS Code.
2. Open the Command Palette (`Ctrl+Shift+P` or `Cmd+Shift+P`).
3. Run **"MinML: Show Live Preview"**.

## Requirements

- Node.js (for building the extension)
- Go (if building from the root `Makefile`)

## Development

To build and install the extension manually for development:

```bash
make vscode-live-preview
```

This will:
1. Build the Go WASM binary.
2. Compile the TypeScript extension source.
3. Install the extension to your local VS Code extensions directory.
