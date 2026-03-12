package main

import (
	"fmt"
	"syscall/js"

	"github.com/dedis/matchertext/go/markup/minml"
)

// convert function that will be exposed to JS
// args[0]: MinML input string
// returns: HTML output string or Error string
func convertString(_ js.Value, args []js.Value) any {
	if len(args) < 1 {
		return "Error: no input provided"
	}
	input := args[0].String()

	output, err := minml.ConvertString(input)

	if err != nil {
		fmt.Println(err.Error())
		return "Invalid input"
	}

	return output
}

func main() {
	// Register the function in the global JS scope
	js.Global().Set("minmlConvert", js.FuncOf(convertString))

	fmt.Println("MinML WASM Converter Initialized")

	// Keep the Go runtime alive
	select {}
}
