GOROOT := $(shell go env GOROOT)

build:
	go build -o minml ./go/markup/minml/cmd/

gen-parser:
	cd dev/tree-sitter && tree-sitter generate

build-lsp:
	go build -o minml-lsp ./go/markup/minml/cmd/lsp/

EXT_NAME := $(shell node -p "require('./dev/vscode/minml-preview/package.json').publisher + '.' + require('./dev/vscode/minml-preview/package.json').name + '-' + require('./dev/vscode/minml-preview/package.json').version")

# Detect OS for VS Code extensions directory
ifeq ($(OS),Windows_NT)
    VSCODE_EXT_DIR := $(USERPROFILE)/.vscode/extensions/$(EXT_NAME)
    VSCODE_EXT_GLOB := $(USERPROFILE)/.vscode/extensions/
else
    VSCODE_EXT_DIR := $(HOME)/.vscode/extensions/$(EXT_NAME)
    VSCODE_EXT_GLOB := $(HOME)/.vscode/extensions/
endif

build-wasm:
	cp "$(GOROOT)/lib/wasm/wasm_exec.js" ./out/wasm/wasm_exec.js
	GOOS=js GOARCH=wasm go build -o ./out/wasm/main.wasm ./go/wasm/main.go
	cp ./out/wasm/wasm_exec.js ./dev/vscode/minml-preview/media/wasm_exec.js
	cp ./out/wasm/main.wasm ./dev/vscode/minml-preview/media/main.wasm

vscode-live-preview: build-wasm build-lsp
	@echo "Checking for npm..."
	@npm --version > /dev/null 2>&1 || (echo "Error: npm is not installed. Please install Node.js and npm." && exit 1)
	@echo "Building VS Code extension..."
	cd dev/vscode/minml-preview && npm ci && npm run compile
	@echo "Installing extension to $(VSCODE_EXT_DIR)..."
	@echo "Removing stale minml extension installations..."
	rm -rf "$(VSCODE_EXT_DIR)"
	@find "$(VSCODE_EXT_GLOB)" -maxdepth 1 -name "*minml*" ! -path "$(VSCODE_EXT_DIR)" -exec rm -rf {} + 2>/dev/null || true
	mkdir -p "$(VSCODE_EXT_DIR)"
	cp -R dev/vscode/minml-preview/dist "$(VSCODE_EXT_DIR)/"
	cp -R dev/vscode/minml-preview/media "$(VSCODE_EXT_DIR)/"
	cp dev/vscode/minml-preview/package.json "$(VSCODE_EXT_DIR)/"
	cp dev/vscode/minml-preview/language-configuration.json "$(VSCODE_EXT_DIR)/"
	cp minml-lsp "$(VSCODE_EXT_DIR)/"
	chmod +x "$(VSCODE_EXT_DIR)/minml-lsp"
	@echo "Done! Please restart VS Code to use the extension."
