// Package minml is a command-line tool to Convert MinML to XML.
//
// This tool is currently extremely "minimal"
// and could be improved in many ways:
// e.g., to Convert to either HTML or XML or other output formats;
// to Convert in the other direction from other formats to MinML;
// or merely to validate and display information about MinML code.
//
// Usage:
//
//	minml [COMMAND] <input.minml> [OPTIONS]
//
// Commands:
//   - Convert: Parse MinML and write HTML to stdout (default)
//   - server:  Start an HTTP server for MinML conversion
//
// Examples:
//
//	minml input.minml
//	minml Convert input.minml
//	minml server input.minml
package main

import (
	"fmt"
	"log"
	"os"

	"github.com/dedis/matchertext/go/markup/html"
	"github.com/dedis/matchertext/go/markup/minml"
)

const usage = `MinML Command-Line Tool

USAGE:
    %s [COMMAND] <input.minml> [OPTIONS]

ARGS:
    <input.minml>    MinML source file

COMMANDS:
    help                                      Print this help message
    Convert <file.minml>                      Parse MinML and write HTML to stdout (default)
    server  <file|directory> [--port 8080]    Start an HTTP server for MinML conversion

DESCRIPTION:
    If no command is given, defaults to 'Convert'.

EXAMPLES:
    %[1]s input.minml
    %[1]s Convert input.minml
    %[1]s server input.minml
`

func main() {
	args := os.Args

	if len(args) < 2 {
		printUsage(args[0])
		os.Exit(1)
	}

	command, inputPath, rest := parseArgs(args)

	switch command {
	case "Convert":
		if len(rest) > 0 {
			log.Fatalf("'Convert' takes no extra arguments")
		}
		Convert(inputPath)
	case "server":
		Server(inputPath, rest)
	default:
		log.Fatalf("Unknown command: %s", command)
	}
}

// parseArgs parses CLI arguments into command, input path, and remaining args.
//
// Format: minml [COMMAND] <input.minml> [OPTIONS]
// Defaults to "Convert" if the first argument is not a known command.
func parseArgs(args []string) (command string, inputPath string, rest []string) {
	switch args[1] {
	case "help":
		printUsage(args[0])
		os.Exit(0)
	case "Convert", "server":
		if len(args) < 3 {
			log.Fatalf("'%s' requires an input file", args[1])
		}
		command = args[1]
		inputPath = args[2]
		rest = args[3:]
	default:
		command = "Convert"
		inputPath = args[1]
		rest = args[2:]
	}

	if _, err := os.Stat(inputPath); os.IsNotExist(err) {
		log.Fatalf("Input file does not exist: %s", inputPath)
	}

	return command, inputPath, rest
}

// printUsage prints the help message to stderr.
func printUsage(program string) {
	fmt.Fprintf(os.Stderr, usage, program)
}

// Convert parses a MinML source file and writes the HTML output to stdout.
func Convert(sourceFile string) {
	file, err := os.Open(sourceFile)
	if err != nil {
		log.Fatalf("Error opening %v: %v", sourceFile, err.Error())
	}
	defer file.Close()

	mp := minml.NewTreeParser(file)
	ns, err := mp.ParseAST()
	if err != nil {
		log.Fatalf("Error parsing %v: %v", sourceFile, err.Error())
	}

	enc := html.NewTreeWriter(os.Stdout)
	if err := enc.WriteAST(ns); err != nil {
		log.Fatalf("Error encoding %v: %v", sourceFile, err.Error())
	}
}
