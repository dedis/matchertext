package matchertext

import (
	"bufio"
	"fmt"
	"io"
)

type Handler interface {
	Byte(c byte) error                // handle any non-matcher byte
	Pair(o, c byte, p Callback) error // handle a matcher pair
}

type Callback func(h Handler) error

// Fast and simple streaming matchertext parser.
type Parser struct {
	r io.ByteReader

	b int		// byte from ungetc witing to be re-getc'd
	last byte	// last byte we read from r

	c Callback

	ofs int64	// byte offset in source starting from 0
	line int	// line number starting from 1
	col int		// column number starting from 1 (counting bytes)

	// If HandleError is non-nil,
	// then the parser invokes it on encountering any syntax error
	// (but not on I/O errors such as end-of-file).
	// HandleError may return the same or a different error
	// to stop parsing, or may return nil to continue despite the error.
	HandleError func(err error) error
}

func NewParser(r io.Reader) *Parser {
	return (&Parser{}).SetReader(r)
}

func (p *Parser) SetReader(r io.Reader) *Parser {
	br, ok := r.(io.ByteReader)
	if !ok {
		br = bufio.NewReader(r)
	}
	return p.SetByteReader(br)
}

func (p *Parser) SetByteReader(r io.ByteReader) *Parser {
	p.r = r
	if p.c == nil {
		p.c = func(h Handler) error {
			_, e := p.parseText(h)
			return e
		}
	}

	// initialize logical position counters
	p.ofs = 0
	p.line = 1
	p.col = 1

	return p
}

// Parse a complete matchertext stream until the end.
// Returns nil on successful parsing until end-of-file (EOF).
func (p *Parser) Parse(h Handler) error {
	c, e := p.parseText(h)
	if e == io.EOF {
		return nil // successful complete parse
	}
	if e == nil {
		return p.matcherError(fmt.Sprintf(
			"unmatched closer %v", string(byte(c))))
	}
	return e // other error
}

// Parse text within a matchertext until an unmatched closer or EOF.
// Returns the terminating closer character, or -1 on EOF or error.
func (p *Parser) parseText(h Handler) (int, error) {
	for {
		b, e := p.getc()
		if e != nil {
			return -1, e
		}
		switch b {

		// When we see an opener, recursively parse the pair
		case '(':
			e = p.pair(h, '(', ')')
		case '[':
			e = p.pair(h, '[', ']')
		case '{':
			e = p.pair(h, '{', '}')

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

// Parse a matched pair and everything in between.
func (p *Parser) pair(h Handler, o, c byte) error {

	// Invoke the matched-pair handler,
	// which will in turn call us back to parse the content of the pair.
	h.Pair(o, c, p.c)

	// Ensure that the content was closed by the correct matcher.
	b, e := p.getc()
	if e == io.EOF {
		return p.matcherError(fmt.Sprintf(
			"unmatched opener %v", string(b)))
	}
	if e != nil {
		return e
	}
	if b != c {
		return p.matcherError(fmt.Sprintf(
			"opener %v closed with mismatched %v",
			string(o), string(b)))
	}
	return nil
}

func (p *Parser) getc() (b byte, e error) {

	// re-return any ungetc'd byte first
	if p.b >= 0 {
		b = byte(p.b)
		p.b = -1
		return
	}

	// advance our logical position based on last byte read
	p.ofs++
	p.col++
	if p.last == '\n' {
		p.line++
		p.col = 1
	}

	// read the next byte from the input stream
	b, e = p.r.ReadByte()
	p.last = b
	return
}

func (p *Parser) ungetc(b byte) {
	p.b = int(b)
}

func (p *Parser) matcherError(msg string) error {
	return &MatcherError{msg, p.ofs, p.line, p.col}
}


type MatcherError struct {
	msg string
	ofs int64
	line, col int
}

func (e *MatcherError) Error() string {
	return e.msg
}

func (e *MatcherError) Offset() int64 {
	return e.ofs
}

func (e *MatcherError) Position() (line int, column int) {
	return e.line, e.col
}

