package util

import (
	"bufio"
	"io"
)

// Interface AtomWriter contains the standard Write methods
// for the basic atomic types that buffered I/O steams in Go normally support:
// namely bytes, runes, strings, and byte slices.
type AtomWriter interface {
	Write(p []byte) (n int, err error)      // write byte slice
	WriteByte(b byte) error                 // write a single byte
	WriteRune(r rune) (size int, err error) // write a UTF-8 rune
	WriteString(s string) (int, error)      // write a string
}

// Return a AtomWriter given an arbitrary io.Writer w.
// If w already supports all the AtomWriter methods, just returns w.
// Otherwise, creates and returns a bufio.Writer on top of w.
// The returned AtomWriter may need to be flushed at end of output.
func ToAtomWriter(w io.Writer) AtomWriter {
	if bw, ok := w.(AtomWriter); ok {
		return bw
	}
	return bufio.NewWriter(w)
}

type flusher interface {
	Flush() error
}

// Flush an io.Writer w if it has a Flush() method.
// If not, quietly does nothing.
func Flush(w io.Writer) error {
	if f, ok := w.(flusher); ok {
		return f.Flush()
	}
	return nil
}
