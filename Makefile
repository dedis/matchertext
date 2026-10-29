GOROOT := $(shell go env GOROOT)

.PHONY: all build

all: build

# Windows needs the .exe suffix that `go build -o` does not add
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

build:
	go build -o minml$(EXE) ./go/markup/minml/cmd/
