GOROOT := $(shell go env GOROOT)

build:
	go build -o minml ./go/markup/minml/cmd/

build-wasm:
	cp "$(GOROOT)/lib/wasm/wasm_exec.js" ./out/wasm/wasm_exec.js
	GOOS=js GOARCH=wasm go build -o ./out/wasm/main.wasm ./go/wasm/main.go