package matchertext

import (
	"bufio"
	"fmt"
	"io"
)

// Handler is an interface representing client logic to handle matchertext.
//
// The matchertext parser invokes Byte on each non-matcher byte encountered.
// Byte is normally expected simply to consume the byte and return,
// but it may recursively re-invoke the Parser if desired.
//
// On encountering an open parenthesis, square bracket, or curly brace,
// the parser invokes Open with the open character o and matching closer c.
// Open is expected to consume the opener, the content, and matching closer,
// typically by invoking the parser's Pair method recursively.
type Handler interface {
	Byte(c byte) error    // handle any non-matcher byte
	Open(o, c byte) error // handle the opener of a matching pair
}

// Fast and simple streaming matchertext parser.
type Parser struct {
	r io.ByteReader

	b    int // byte from ungetc witing to be re-getc'd
	last int // last byte we read from r

	ofs  int64 // byte offset in source starting from 0
	line int   // line number starting from 1
	col  int   // column number starting from 1 (counting bytes)

	unclosed bool // the last ReadPair returned without consuming its closer
	depth    int  // number of pairs whose content ReadPair is parsing

	// If HandleError is non-nil,
	// then the parser invokes it on encountering any syntax error
	// (but not on I/O errors such as end-of-file).
	// HandleError may return the same or a different error
	// to stop parsing, or may return nil to continue despite the error.
	// Continuing closes an unmatched opener at end-of-file,
	// closes a pair at a mismatched closer and leaves that closer unread,
	// leaves the unexpected byte unread where an opener was expected,
	// and skips an unmatched closer in ReadAll.
	HandleError func(err error) error
}

// NewParser creates and returns a new Parser that reads from stream r.
func NewParser(r io.Reader) *Parser {
	return (&Parser{}).SetReader(r)
}

// SetReader initializes parser p to read from stream r and returns p.
// A single Parser object may be reused to parse successive streams.
func (p *Parser) SetReader(r io.Reader) *Parser {
	br, ok := r.(io.ByteReader)
	if !ok {
		br = bufio.NewReader(r)
	}
	return p.init(br)
}

func (p *Parser) init(r io.ByteReader) *Parser {
	p.r = r
	p.b = -1
	p.last = -1
	p.depth = 0
	p.unclosed = false

	// initialize logical position counters
	p.ofs = 0
	p.line = 1
	p.col = 1

	return p
}

// ReadText parses a matchertext stream until it encounters end-of-file (EOF)
// or some other error.
//
// On encountering any non-matcher byte, ReadText invokes h.Byte to handle it.
// The client's Byte handler may return a non-nil error to cease parsing text
// without consuming the last offered byte.
//
// On finding an open matcher (parenthesis, square bracket, or curly brace),
// Text invokes h.Open to handle the matchertext substring.
// The Open handler is normally expected to invoke the parser's Pair method
// to parse the opener, contents, and matching closer.
//
// Returns nil on successful parsing until end-of-file (EOF).
func (p *Parser) ReadAll(h Handler) error {
	for {
		c, e := p.ReadText(h)
		if e == io.EOF {
			return nil // successful complete parse
		}
		if e != nil {
			return e // other error
		}
		e = p.Fail(fmt.Sprintf("unmatched closer %v", string(byte(c))))
		if e != nil {
			return e
		}
		p.getc() // recover by skipping the closer that ReadText left unread
	}
}

// Parse text from a matchertext stream until encountering
// either an unmatched closer character or end-of-file (EOF).
// On finding an unmatched closer, returns the closer without consuming it.
// Returns -1 on EOF or error.
func (p *Parser) ReadText(h Handler) (closer int, err error) {
	for {
		// Look ahead one byte in the stream
		b, e := p.getc()
		if e != nil {
			return -1, e
		}

		switch b {

		// Handle any openers that we encounter.
		// We expect the handler to invoke Pair
		// to consume the entire delimited substring.
		case '(':
			p.ungetc(b)
			e = h.Open('(', ')')
		case '[':
			p.ungetc(b)
			e = h.Open('[', ']')
		case '{':
			p.ungetc(b)
			e = h.Open('{', '}')

		// When we see a closer, our current matchertext level is done
		case ')', ']', '}':
			p.ungetc(b)
			return int(b), nil

		// Handle normal nonmatcher bytes directly
		default:
			e = h.Byte(b)
		}
		if e != nil {
			return -1, e
		}
	}
}

// Pair parses a matching pair of matchers and everything in between.
// Pair expects to see the specific open matcher o first,
// then it consumes arbitrary text including nested pairs,
// and finally it consumes the matching closer c.
// Returns nil if the whole matcher-delimited sequence was parsed successfully,
// or a non-nil error if anything goes wrong.
func (p *Parser) ReadPair(h Handler, o, c byte) error {
	p.unclosed = true

	// First consume the opener and make sure it is the expected one.
	b, e := p.getc()
	if e == io.EOF || (e == nil && b != o) {
		if e == nil {
			p.ungetc(b)
		}
		return p.Fail(fmt.Sprintf("expecting opener %v", string(o)))
	}
	if e != nil {
		return e
	}
	ofs, line, col := p.ofs, p.line, p.col

	// Parse the intervening text delimited by the matcher pair.
	p.depth++
	_, e = p.ReadText(h)
	p.depth--
	if e == io.EOF {
		p.unclosed = true
		return p.fail(&SyntaxError{fmt.Sprintf("unmatched opener %v", string(o)), ofs, line, col})
	}
	if e != nil {
		return e
	}

	// Ensure that the content was closed by the correct matcher.
	// ReadText returned at a closer, so this read cannot reach end-of-file.
	b, e = p.getc()
	if e != nil {
		return e
	}
	if b != c {
		p.ungetc(b)
		p.unclosed = true
		return p.Fail(fmt.Sprintf(
			"opener %v closed with mismatched %v",
			string(o), string(b)))
	}
	p.unclosed = false
	return nil
}

// Depth returns the number of matcher pairs enclosing the current position.
func (p *Parser) Depth() int {
	return p.depth
}

// Unclosed reports whether the last ReadPair to return did not consume its closer.
// That happens only when HandleError let parsing continue after a syntax error.
func (p *Parser) Unclosed() bool {
	return p.unclosed
}

func (p *Parser) getc() (b byte, e error) {

	// re-return any ungetc'd byte first
	if p.b >= 0 {
		b = byte(p.b)
		p.b = -1
		return
	}

	// advance our logical position based on last byte read
	if p.last >= 0 {
		p.ofs++
		p.col++
		if p.last == '\n' {
			p.line++
			p.col = 1
		}
	}

	// read the next byte from the input stream
	b, e = p.r.ReadByte()
	if e != nil {
		p.last = -1 // stay at the end-of-file position on repeated reads
		return
	}

	p.last = int(b)
	return
}

func (p *Parser) ungetc(b byte) {
	p.b = int(b)
}

// Fail reports a syntax error at the current position.
// It returns HandleError's result if HandleError is set, or else the error.
// A nil result means the caller should recover and continue parsing.
func (p *Parser) Fail(msg string) error {
	return p.fail(p.SyntaxError(msg))
}

func (p *Parser) fail(e *SyntaxError) error {
	if p.HandleError != nil {
		return p.HandleError(e)
	}
	return e
}

// Create an object describing a syntax error while parsing matchertext.
// The matchertext parser only creates errors due to unmatched matchers,
// but clients can use this method to report language-specific syntax errors.
func (p *Parser) SyntaxError(msg string) *SyntaxError {
	return &SyntaxError{msg, p.ofs, p.line, p.col}
}

// ReadByte reads and returns the next byte from the matchertext stream,
// without any special handling of any matcher characters encountered.
func (p *Parser) ReadByte() (b byte, err error) {
	return p.getc()
}

// PeekByte returns the next byte from the underlying matchertext stream,
// without consuming it.
func (p *Parser) PeekByte() (b byte, err error) {
	b, err = p.getc()
	if err == nil {
		p.ungetc(b)
	}
	return
}

// Adjust position (line,col) to the position after reading byte b.
func nextPos(line, col int, b byte) (nextLine, nextCol int) {
	col++
	if b == '\n' {
		line++
		col = 1
	}
	return line, col
}

// Offset returns the current byte offset within the matchertext being parsed.
func (p *Parser) Offset() int64 {
	return p.ofs
}

// Position returns the current line and column number
// within the matchertext being parsed.
func (p *Parser) Position() (line int, col int) {
	return p.line, p.col
}

// SyntaxError describes any syntax error the parser encounters.
type SyntaxError struct {
	msg       string
	ofs       int64
	line, col int
}

// Error returns a human-readable description of the error.
func (e *SyntaxError) Error() string {
	return fmt.Sprintf("%v:%v %v", e.line, e.col, e.msg)
}

// Message returns the description of the error without its position.
func (e *SyntaxError) Message() string {
	return e.msg
}

// Offset returns the byte position at which the error occurred.
func (e *SyntaxError) Offset() int64 {
	return e.ofs
}

// Position returns the line and column number at which the error occurred.
func (e *SyntaxError) Position() (line int, column int) {
	return e.line, e.col
}
