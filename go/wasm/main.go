package main

import (
	"fmt"
	"syscall/js"
)

// convert function that will be exposed to JS
// args[0]: MinML input string
// returns: HTML output string or Error string
func convert(_ js.Value, args []js.Value) any {
	if len(args) < 1 {
		return "Error: no input provided"
	}
	input := args[0].String()

	fmt.Println("This is not working currently")

	return input
}

func main() {
	// Register the function in the global JS scope
	js.Global().Set("minmlConvert", js.FuncOf(convert))

	fmt.Println("MinML WASM Converter Initialized")

	// Keep the Go runtime alive
	select {}
}
