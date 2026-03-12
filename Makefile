GOROOT := $(shell go env GOROOT)

build:
	go build -o minml ./go/markup/minml/cmd/

build-wasm:
	cp "$(GOROOT)/lib/wasm/wasm_exec.js" ./out/wasm/wasm_exec.js
	GOOS=js GOARCH=wasm go build -o ./out/wasm/main.wasm ./go/wasm/main.go
	cp ./out/wasm/wasm_exec.js ./devtools/minml-preview/media/wasm_exec.js
	cp ./out/wasm/main.wasm ./devtools/minml-preview/media/main.wasm