// Package minml is a command-line tool to convert MinML to XML.
//
// This tool is currently extremely "minimal"
// and could be improved in many ways:
// e.g., to convert to either HTML or XML or other output formats;
// to convert in the other direction from other formats to MinML;
// or merely to validate and display information about MinML code.
package main

import (
	"log"
	"os"

	"github.com/dedis/matchertext/go/markup/html"
	"github.com/dedis/matchertext/go/markup/minml"
)

func main() {
	args := os.Args
	if len(args) != 2 {
		log.Fatalf("Usage: %v sourcefile\n", args[0])
	}
	sourcefile := args[1]

	file, err := os.Open(sourcefile)
	if err != nil {
		log.Fatalf("Error opening %v: %v", sourcefile, err.Error())
	}

	mp := minml.NewDecoder(file)
	ns, err := mp.Decode()
	if err != nil {
		log.Fatalf("Error parsing %v: %v", sourcefile, err.Error())
	}

	enc := html.NewEncoder(os.Stdout)
	if err := enc.Encode(ns); err != nil {
		log.Fatalf("Error encoding %v: %v", sourcefile, err.Error())
	}
}
