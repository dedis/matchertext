package util

import (
	"bufio"
	"io"
)

// ToByteScanner returns an AtomReader given an arbitrary io.Reader r.
// If e already supports all the AtomReader methods, just returns r.
// Otherwise, creates and returns a bufio.Reader on top of r.
func ToByteScanner(r io.Reader) io.ByteScanner {
	if br, ok := r.(io.ByteScanner); ok {
		return br
	}
	return bufio.NewReader(r)
}

// Interface AtomScanner contains the standard Read and Unread methods
// for the basic atomic types that buffered I/O steams in Go normally support:
// namely bytes, runes, strings, and byte slices.
type AtomScanner interface {
	Read(p []byte) (n int, err error)
	ReadByte() (byte, error)
	ReadRune() (r rune, size int, err error)
	UnreadByte() error
	UnreadRune() error
	ReadBytes(delim byte) (line []byte, err error)
	ReadString(delim byte) (string, error)
}

// ToAtomReader returns an AtomReader given an arbitrary io.Reader r.
// If e already supports all the AtomReader methods, just returns r.
// Otherwise, creates and returns a bufio.Reader on top of r.
func ToAtomScanner(r io.Reader) AtomScanner {
	if br, ok := r.(AtomScanner); ok {
		return br
	}
	return bufio.NewReader(r)
}

// Interface AtomWriter contains the standard Write methods
// for the basic atomic types that buffered I/O steams in Go normally support:
// namely bytes, runes, strings, and byte slices.
type AtomWriter interface {
	Write(p []byte) (n int, err error)      // write byte slice
	WriteByte(b byte) error                 // write a single byte
	WriteRune(r rune) (size int, err error) // write a UTF-8 rune
	WriteString(s string) (int, error)      // write a string
}

// ToAtomWriter returns an AtomWriter given an arbitrary io.Writer w.
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
