package tree_sitter_minml

// #cgo CFLAGS: -std=c11 -fPIC
// #include "../../src/parser.c"
// #if __has_include("../../src/scanner.c")
// #include "../../src/scanner.c"
// #endif
import "C"

import "unsafe"

// Language returns the tree-sitter Language for MinML.
func Language() unsafe.Pointer {
	return unsafe.Pointer(C.tree_sitter_minml())
}
