package main

import (
	//	"fmt"
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

	ns, err := minml.Parse(file)
	if err != nil {
		log.Fatalf("Error parsing %v: %v", sourcefile, err.Error())
	}

	enc := html.NewEncoder(os.Stdout)
	if err := enc.Encode(ns); err != nil {
		log.Fatalf("Error encoding %v: %v", sourcefile, err.Error())
	}
}
