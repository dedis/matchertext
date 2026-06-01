// Package minml is a command-line tool to convert MinML to XML.
//
// This tool is currently extremely "minimal"
// and could be improved in many ways:
// e.g., to convert to either HTML or XML or other output formats;
// to convert in the other direction from other formats to MinML;
// or merely to validate and display information about MinML code.
//
// Usage:
//
//	minml [COMMAND] <input.minml> [OPTIONS]
//
// Commands:
//   - convert: Parse MinML and write HTML to stdout (default)
//   - server:  Start an HTTP server for MinML conversion
//
// Examples:
//
//	minml input.minml
//	minml convert input.minml
//	minml server input.minml
package main

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"

	"github.com/dedis/matchertext/go/markup/minml"
	"github.com/dedis/matchertext/go/markup/xml"
)

const usage = `MinML Command-Line Tool

USAGE:
    %s [COMMAND] <input> [OPTIONS]

COMMANDS:
    help                                  Print this help message
    convert  <file.minml>                 Parse MinML and write HTML to stdout (default)
    from-xml <file.xml> [OPTIONS]         Convert XML to MinML (auto-creates <file>.m by default)
    server   <file|directory> [OPTIONS]   Start an HTTP server for MinML conversion

OPTIONS (from-xml):
    --output <file>                       Output file path (default: input path with .m extension)

OPTIONS (server):
    --port <port>                         Port to listen on (default: 8080)
    --no-open                             Prevents the browser from automatically opening the served file/directory
	--disk                                Creates a disk build instead of a in-memory build
	--extensions <ext1,ext2,...>          Comma separated list of additional minml file extensions

DESCRIPTION:
    If no command is given, defaults to 'convert'.

EXAMPLES:
    %[1]s input.minml
    %[1]s convert input.minml
    %[1]s from-xml input.xml
    %[1]s from-xml input.xml --output output.m
    %[1]s server input.minml
`

const CmdConvert = "convert"
const CmdFromXML = "from-xml"
const CmdServer = "server"

func main() {
	args := os.Args

	if len(args) < 2 {
		printUsage(args[0])
		os.Exit(1)
	}

	command, inputPath, rest := parseArgs(args)
	extensions := []string{"minml", "m"}

	switch command {
	case CmdConvert:
		for i := 0; i < len(rest); i++ {
			switch rest[i] {
			case "--extensions":
				i++
				if i >= len(rest) {
					log.Fatal("--extensions requires a value")
				}
				extensions = append(extensions, strings.Split(rest[i], ",")...)
			default:
				log.Fatal("unknown option for '", CmdConvert, "': ", rest[i])
			}
		}

		if err := minml.Convert(inputPath, os.Stdout, true, extensions); err != nil {
			log.Fatal(err)
		}
	case CmdFromXML:
		outputPath := strings.TrimSuffix(inputPath, filepath.Ext(inputPath)) + ".m"
		for i := 0; i < len(rest); i++ {
			switch rest[i] {
			case "--output":
				i++
				if i >= len(rest) {
					log.Fatal("--output requires a value")
				}
				outputPath = rest[i]
			default:
				log.Fatal("unknown option for '", CmdFromXML, "': ", rest[i])
			}
		}

		f, err := os.Open(inputPath)
		if err != nil {
			log.Fatal(err)
		}
		defer f.Close()

		ns, err := xml.NewTreeParser(f).ParseAST()
		if err != nil {
			log.Fatalf("parsing %s: %v", inputPath, err)
		}

		out, err := os.Create(outputPath)
		if err != nil {
			log.Fatalf("creating output file %s: %v", outputPath, err)
		}
		defer out.Close()

		if err := minml.NewTreeWriter(out).WriteAST(ns); err != nil {
			log.Fatalf("writing MinML: %v", err)
		}
		fmt.Fprintf(os.Stderr, "wrote %s\n", outputPath)
	case CmdServer:
		port := "8080"
		noOpen := false
		diskBuild := false
		for i := 0; i < len(rest); i++ {
			switch rest[i] {
			case "--port":
				i++
				if i >= len(rest) {
					log.Fatal("--port requires a value")
				}
				port = rest[i]
			case "--no-open":
				noOpen = true
			case "--disk":
				diskBuild = true
			case "--extensions":
				i++
				if i >= len(rest) {
					log.Fatal("--extensions requires a value")
				}
				extensions = append(extensions, strings.Split(rest[i], ",")...)
			default:
				log.Fatal("unknown option for '", CmdServer, "': ", rest[i])
			}
		}
		Server(inputPath, port, noOpen, diskBuild, extensions)
	default:
		log.Fatalf("Unknown command: %s", command)
	}
}

// parseArgs parses CLI arguments into command, input path, and remaining args.
//
// Format: minml [COMMAND] <input.minml> [OPTIONS]
// Defaults to CmdConvert if the first argument is not a known command.
func parseArgs(args []string) (command string, inputPath string, rest []string) {
	switch args[1] {
	case "help":
		printUsage(args[0])
		os.Exit(0)
	case CmdConvert, CmdFromXML, CmdServer:
		if len(args) < 3 {
			log.Fatalf("'%s' requires an input file", args[1])
		}
		command = args[1]
		inputPath = args[2]
		rest = args[3:]
	default:
		command = CmdConvert
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
