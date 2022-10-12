
package matchertext

import (
	"io"
)

// Fast and simple streaming matchertext parser.
type Parser interface {
	Parse(h Handler) error
}

type Handler interface {
	Byte(c byte) error		// handle any non-matcher byte
	Pair(o, c byte, p Parser) error	// handle a matcher pair
}

type parser struct {
	r io.ByteReader
	b int
}

func NewParser(r io.ByteReader) Parser {
	return &parser{r, -1}
}

func (p *parser) Parse(h Handler) error {
	for {
		b, e := p.getc()
		if e != nil {
			return e
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

		// Handle normal nonmatcher bytes directly
		default:
			e = h.Byte(b)
		}
		if e != nil {
			return e
		}
	}
}

// Parse a matched pair and everything in between.
func (p *parser) pair(h Handler, o, c byte) error {

	// Invoke the matched-pair handler,
	// which will in turn call us back to parse the content of the pair.
	h.Pair(o, c, p)

	// Ensure that the content was closed by the correct matcher.
	b, e := p.getc()
	if e == io.EOF {
		return p.syntaxError("unclosed opener")
	}
	if e != nil {
		return e
	}
	if b != c {
		return p.syntaxError("mismatched matchers")
	}
	return nil
}

func (p *parser) getc() (b byte, e error) {
	if p.b >= 0 {
		b = byte(p.b)
		p.b = -1
		return
	}
	b, e = p.r.ReadByte()
	return
}

func (p *parser) ungetc(b byte) {
	p.b = int(b)
}

func (p *parser) syntaxError(msg string) error {
	return &syntaxError{msg}
}

type syntaxError struct {
	msg string
}

func (e *syntaxError) Error() string {
	return e.msg
}

