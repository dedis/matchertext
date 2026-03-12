GOROOT := $(shell go env GOROOT)

build:
	go build -o minml ./go/markup/minml/cmd/

# Detect OS for VS Code extensions directory
ifeq ($(OS),Windows_NT)
    VSCODE_EXT_DIR := $(USERPROFILE)/.vscode/extensions/minml-preview
else
    VSCODE_EXT_DIR := $(HOME)/.vscode/extensions/minml-preview
endif

build-wasm:
	cp "$(GOROOT)/lib/wasm/wasm_exec.js" ./out/wasm/wasm_exec.js
	GOOS=js GOARCH=wasm go build -o ./out/wasm/main.wasm ./go/wasm/main.go
	cp ./out/wasm/wasm_exec.js ./devtools/minml-preview/media/wasm_exec.js
	cp ./out/wasm/main.wasm ./devtools/minml-preview/media/main.wasm

vscode-live-preview: build-wasm
	@echo "Checking for npm..."
	@npm --version > /dev/null 2>&1 || (echo "Error: npm is not installed. Please install Node.js and npm." && exit 1)
	@echo "Building VS Code extension..."
	cd devtools/minml-preview && npm install && npm run compile
	@echo "Installing extension to $(VSCODE_EXT_DIR)..."
	mkdir -p "$(VSCODE_EXT_DIR)"
	cp -R devtools/minml-preview/dist "$(VSCODE_EXT_DIR)/"
	cp -R devtools/minml-preview/media "$(VSCODE_EXT_DIR)/"
	cp devtools/minml-preview/package.json "$(VSCODE_EXT_DIR)/"
	@echo "Done! Please restart VS Code to use the extension."