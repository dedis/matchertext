package minml

import (
	"bytes"
	"fmt"
	"io"
	"io/fs"
	"log"
	"os"
	"path/filepath"
	"slices"
	"strings"

	"github.com/dedis/matchertext/go/markup/ast"
	"github.com/dedis/matchertext/go/markup/html"
	"github.com/dedis/matchertext/go/markup/xml"
)

// Convert parses a MinML source file and writes the HTML or XML output to w,
// as its document type ![html] or ![xml] declares; without one, the output is HTML.
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

// ConvertString takes MinML string content and returns the HTML or XML output.
func ConvertString(content string) (string, error) {
	r := strings.NewReader(content)
	buf := bytes.NewBuffer(nil)

	err := convertFromReader(r, buf, "string")
	if err != nil {
		return "", err
	}
	return buf.String(), nil
}

// convert converts a single minml file to HTML or XML
// Non-.minml files are ignored.
func convert(path string, w io.Writer, extensions []string) error {
	if isMinml, _ := IsMinmlFile(path, extensions); !isMinml {
		return nil
	}

	file, err := os.Open(path)
	if err != nil {
		return fmt.Errorf("opening %v: %w", path, err)
	}
	defer file.Close()

	return convertFromReader(file, w, path)
}

// convertFromReader parses MinML from r and writes HTML or XML to w.
// name is used only in error messages.
func convertFromReader(r io.Reader, w io.Writer, name string) error {
	mp := NewTreeParser(r).WithTransformer(EntityTransformer).WithTransformer(QuoteTransformer)
	ns, err := mp.ParseAST()
	if err != nil {
		return fmt.Errorf("parsing %v: %w", name, err)
	}

	if err := writerFor(ns, w).WriteAST(ns); err != nil {
		return fmt.Errorf("encoding %v: %w", name, err)
	}

	return nil
}

// writerFor returns the writer for the document type that ns declares:
// XML for ![xml], and HTML for ![html] or no document type.
func writerFor(ns []ast.Node, w io.Writer) interface{ WriteAST([]ast.Node) error } {
	if ast.DoctypeOf(ns) == "xml" {
		return xml.NewTreeWriter(w)
	}
	return html.NewTreeWriter(w)
}

// IsXMLDocument reports whether MinML source src declares the document type ![xml],
// which only its first bytes can be.
func IsXMLDocument(src []byte) bool {
	return bytes.HasPrefix(src, []byte("![xml]"))
}

// IsMinmlFile checks if the file at the given path uses a supported minml extension.
// If it does it also returns the file extension.
func IsMinmlFile(path string, extensions []string) (bool, string) {
	ext := filepath.Ext(path)
	if ext == "" {
		return false, ""
	}
	extension := ext[1:]
	if !slices.Contains(extensions, extension) {
		return false, ""
	}

	return true, extension
}
