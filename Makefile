GOROOT := $(shell go env GOROOT)

.PHONY: all build build-lsp build-wasm vscode-live-preview neovim-plugin emacs-plugin sublime-plugin jetbrains-plugin gen-parser test-grammar zed-extension

all: build

# Windows needs the .exe suffix that `go build -o` does not add
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

build:
	go build -o minml$(EXE) ./go/markup/minml/cmd/

build-lsp:
	go build -o minml-lsp$(EXE) ./go/markup/minml/cmd/lsp/

neovim-plugin: build-lsp
	mkdir -p dev/neovim/bin
	cp minml-lsp$(EXE) dev/neovim/bin/

emacs-plugin: build-lsp
	mkdir -p dev/emacs/bin
	cp minml-lsp$(EXE) dev/emacs/bin/

sublime-plugin: build-lsp
	mkdir -p dev/sublime/bin
	cp minml-lsp$(EXE) dev/sublime/bin/

# Needs JDK 21 in JAVA_HOME; every JetBrains IDE bundles one.
jetbrains-plugin:
	cd dev/jetbrains && ./gradlew buildPlugin

# The generated src/ is committed, because Zed and Helix build the grammar from it.
gen-parser:
	cd dev/tree-sitter && tree-sitter generate

# Checks that the grammar and the Go parser accept the same documents with the same constructs.
test-grammar:
	cd dev/tree-sitter && tree-sitter test && go test ./...

# A copy of dev/zed whose grammar comes from this repository at its current commit,
# for "zed: install dev extension". Commit the grammar first.
zed-extension:
	rm -rf out/zed-extension
	mkdir -p out/zed-extension
	cp -RL dev/zed/Cargo.toml dev/zed/Cargo.lock dev/zed/src dev/zed/languages out/zed-extension/
	sed -e '/^\[grammars.minml\]/,/^$$/s|^repository = .*|repository = "file://$(CURDIR)"|' \
	    -e "/^\[grammars.minml\]/,/^$$/s|^rev = .*|rev = \"$$(git rev-parse HEAD)\"|" \
	    dev/zed/extension.toml > out/zed-extension/extension.toml

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
	@echo "Removing stale minml-preview installations..."
	rm -rf "$(VSCODE_EXT_DIR)"
	@find "$(VSCODE_EXT_GLOB)" -maxdepth 1 -name "*minml-preview*" ! -path "$(VSCODE_EXT_DIR)" -exec rm -rf {} + 2>/dev/null || true
	mkdir -p "$(VSCODE_EXT_DIR)"
	cp -R dev/vscode/minml-preview/dist "$(VSCODE_EXT_DIR)/"
	cp -R dev/vscode/minml-preview/media "$(VSCODE_EXT_DIR)/"
	cp dev/vscode/minml-preview/package.json "$(VSCODE_EXT_DIR)/"
	cp dev/vscode/minml-preview/language-configuration.json "$(VSCODE_EXT_DIR)/"
	cp minml-lsp$(EXE) "$(VSCODE_EXT_DIR)/"
	chmod +x "$(VSCODE_EXT_DIR)/minml-lsp$(EXE)"
	@echo "Done! Please restart VS Code to use the extension."
