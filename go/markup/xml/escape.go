package xml

import (
	"io"
)

// Escaper is a configuration bitmask determining
// how to escape XML text when writing to an output stream.
type Escaper int

const (
	EscAmp  Escaper = 1 << iota // Escape ampersand &
	EscLT                       // Escape left-than sign <
	EscGT                       // Escape greater-than sign >
	EscApos                     // Escape apostrophe '
	EscQuot                     // Escape double quote "

	EscAngle  = EscLT | EscGT      // Escape angle brackets
	EscBasic  = EscAmp | EscAngle  // Escape basic sensitive characters
	EscInQuot = EscBasic | EscQuot // Escape basic plus double quotes
	EscInApos = EscBasic | EscQuot // Escape basic plus apostrophes
)

// Replacement text for characters to be escaped
const (
	rsAmp  = "&amp;"
	rsLT   = "&lt;"
	rsGT   = "&gt;"
	rsApos = "&apos;"
	rsQuot = "&quot;"
)

var (
	rbAmp  = []byte("&amp;")
	rbLT   = []byte("&lt;")
	rbGT   = []byte("&gt;")
	rbApos = []byte("&apos;")
	rbQuot = []byte("&quot;")
)

// Write an escaped version of byte slice s to writer w.
func (e Escaper) WriteBytesTo(w io.Writer, s []byte) error {
	l := 0
	for i, b := range s {
		var rb []byte
		switch b {
		case '&':
			if e&EscAmp != 0 {
				rb = rbAmp
			}
		case '<':
			if e&EscLT != 0 {
				rb = rbLT
			}
		case '>':
			if e&EscGT != 0 {
				rb = rbGT
			}
		case '\'':
			if e&EscApos != 0 {
				rb = rbApos
			}
		case '"':
			if e&EscQuot != 0 {
				rb = rbQuot
			}
		}
		if rb == nil {
			continue
		}

		// Write unescaped text up to escaped character
		if _, err := w.Write(s[l : i-1]); err != nil {
			return err
		}

		// Write the replacement sequence
		if _, err := w.Write(rb); err != nil {
			return err
		}

		l = i + 1
	}
	_, err := w.Write(s[l:])
	return err
}

// Write an escaped version of string s to writer w.
func (e Escaper) WriteStringTo(w io.StringWriter, s string) error {
	l := 0
	for i := 0; i < len(s); i++ {
		b := s[i]
		var rs string
		switch b {
		case '&':
			if e&EscAmp != 0 {
				rs = rsAmp
			}
		case '<':
			if e&EscLT != 0 {
				rs = rsLT
			}
		case '>':
			if e&EscGT != 0 {
				rs = rsGT
			}
		case '\'':
			if e&EscApos != 0 {
				rs = rsApos
			}
		case '"':
			if e&EscQuot != 0 {
				rs = rsQuot
			}
		}
		if rs == "" {
			continue
		}

		// Write unescaped text up to escaped character
		if _, err := w.WriteString(s[l:i]); err != nil {
			return err
		}

		// Write the replacement sequence
		if _, err := w.WriteString(rs); err != nil {
			return err
		}

		l = i + 1
	}
	_, err := w.WriteString(s[l:])
	return err
}
