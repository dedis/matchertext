package main

import (
	"fmt"
	"io"
	"io/fs"
	"log"
	"os"
	"path/filepath"
	"slices"

	"github.com/dedis/matchertext/go/markup/html"
	"github.com/dedis/matchertext/go/markup/minml"
)

// Convert parses a MinML source file and writes the HTML output to w.
func Convert(path string, w io.Writer, isStdOut bool, extensions []string) error {
	fi, err := os.Stat(path)
	if err != nil {
		log.Fatal(err)
	}

	// Convert all files from source
	switch mode := fi.Mode(); {
	case mode.IsDir():
		return filepath.WalkDir(path, func(path string, entry fs.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if entry.IsDir() {
				return nil
			}

			if isStdOut {
				fmt.Println("\n" + path + ": ")
			}
			return convert(path, w, extensions)
		})
	case mode.IsRegular():
		if err := convert(path, w, extensions); err != nil {
			return err
		}
	}

	return nil
}

// convert converts a single minml file to HTML
// Non-.minml files are ignored.
func convert(path string, w io.Writer, extensions []string) error {
	extension := filepath.Ext(path)[1:] // Remove the . from the extension
	if !slices.Contains(extensions, extension) {
		return nil
	}

	file, err := os.Open(path)
	if err != nil {
		return fmt.Errorf("opening %v: %w", path, err)
	}
	defer file.Close()

	mp := minml.NewTreeParser(file).WithTransformer(minml.EntityTransformer).WithTransformer(minml.QuoteTransformer)
	ns, err := mp.ParseAST()
	if err != nil {
		return fmt.Errorf("parsing %v: %w", path, err)
	}

	enc := html.NewTreeWriter(w)
	if err := enc.WriteAST(ns); err != nil {
		return fmt.Errorf("encoding %v: %w", path, err)
	}

	return nil
}
