package minml

import (
	"bytes"
	"io"

	"github.com/dedis/matchertext/go/markup/xml"
	"github.com/dedis/matchertext/go/matchertext"
)

// HandlerMarkup represents client logic for handling general XML markup,
// which may contain elements, text, and character references.
// This interface extends HandlerText for handling text and references.
//
// The parser calls Element whenever it encounters an XML element.
// The handler must invoke the provided parseElement function exactly once
// when it is ready to parse the element's attributes and/or content.
type HandlerMarkup interface {
	HandlerText                // Handle plain text and references
	Element(name []byte) error // Handle markup elements
}

// HandlerText represents client logic for handling text markup,
// which may contain plain text and character references.
// The parser will invoke Text for each contiguous sequence of normal text,
// and will invoke Reference on encountering a character reference.
type HandlerText interface {
	Text(text []byte, raw bool) error // Handle plain UTF-8 text
	Reference(name []byte) error      // Handle a character reference
}

// HandlerElement represents client logic for handling an XML element.
// While parsing an element, the parser
// will invoke Attribute one per attribute if the element has attributes,
// then the parser will invoke Content exactly once for this element
// (unless a parse error occurs before getting to the element content).
type HandlerElement interface {
	Attribute(name []byte) error // Handle attributes
	Content() error              // Handle content markup
}

// HandlerComment is an interface that a client may optionally implement,
// as an extension to HandlerText, to obtain and handle the text of comments.
// If the client's HandlerText does not implement this extension,
// the parser will just silently discard all comments.
type HandlerComment interface {
	Comment(text []byte) error // Handle the text comprising a comment
}

// The above three handler interfaces bundled into one struct
type handlers struct {
	m HandlerMarkup
	t HandlerText
	e HandlerElement
}

// NewParser creates and returns a new Parser that reads MinML input from r.
func NewParser(r io.Reader) *Parser {
	p := &Parser{}
	p.SetReader(r)
	return p
}

// Parser represents the state of a MinML parser.
type Parser struct {
	mp matchertext.Parser

	mh mHandler // matchertext callback for markup parsing
	ah aHandler // matchertext callback for attribute parsing
	vh vHandler // matchertext callback for unquoted value parsing
	rh rHandler // matchertext callback for raw matchertext parsing

	buf bytes.Buffer // bytes read so far but not yet consumed
	lmb byte         // last matcher seen for space sucking, 0 if none
	lmp int          // position to suck space after last matcher
	err error

	// markup handlers to use while parsing matchertext
	h handlers
}

func (p *Parser) init() {

	// Initialize back pointers from the matchertext parsing handlers
	p.mh.p = p
	p.ah.p = p
	p.vh.p = p
	p.rh.p = p

	// Clear the parsing state
	p.buf.Reset()
	p.lmb = 0
	p.err = nil
	p.h = handlers{}
}

// SetReader initializes the Parser to read from stream r.
// A single Parser object may be reused to parse successive streams.
func (p *Parser) SetReader(r io.Reader) {
	p.init()
	p.mp.SetReader(r)
}

// ReadAll reads an entire stream of MinML markup until end-of-file (EOF).
// Returns a non-nil error if anything goes wrong.
func (p *Parser) ReadAll(hm HandlerMarkup) error {
	e := p.ReadMarkup(hm)
	if e == nil {
		return p.syntaxError("expected end of file")
	}
	if e != io.EOF {
		return e
	}
	return nil
}

// ReadMarkup reads markup within a MinML stream,
// which may include plain text, character references, and elements.
// ReadMarkup parses markup until it encounters
// the end of a current syntactic construct such as an element,
// or until it encounters end-of-file or another eror.
func (p *Parser) ReadMarkup(hm HandlerMarkup) error {
	return p.read(hm, hm)
}

// ReadText reads text-only markup within a MinML stream,
//
// the end of the current syntactic construct such as an element,
// or until end-of-file or another eror.
func (p *Parser) ReadText(ht HandlerText) error {
	return p.read(nil, ht)
}

// Read non-element text when hm == nil,
// or arbitrary markup when hm != nil.
func (p *Parser) read(hm HandlerMarkup, ht HandlerText) error {

	// Stash the client's parsing handlers
	p.h = handlers{m: hm, t: ht}

	// Use the underlying matchertext parser to parse the structure
	_, e := p.mp.ReadText(p.mh)
	if e == io.EOF {
		e = p.mFlush(false) // Flush any remaining text at EOF
		if e != nil {
			return e
		}
		return io.EOF
	} else if e != nil {
		return e
	}

	// Flush any plain text at the end to the text handler
	return p.mFlush(false)
}

// Matchertext handler for markup parsing
type mHandler struct {
	p *Parser
}

// Between matchers, we just accumulate bytes without processing them.
func (mh mHandler) Byte(b byte) error {
	return mh.p.buf.WriteByte(b)
}

// Handle a matching pair of openers/closers while parsing matchertext.
// The non-matchers immediately preceding the opener is buffered in p.buf.
func (mh mHandler) Open(o, c byte) (e error) {
	p := mh.p

	// Check for elements only when we see bracket or brace openers,
	// and only if we're parsing general rather than text-only markup.
	if o != '(' && p.h.m != nil {

		// See if the opener is the start of a markup element.
		b := p.buf.Bytes()
		if pos := scanStarter(b); pos >= 0 {

			// Truncate the buffer just before the element name
			p.buf.Truncate(pos)

			// Suck space leading up to element name
			p.suckSpace(true)

			// Handle remaining character data before the element
			if e := p.handleText(p.buf.Len(), false); e != nil {
				return e
			}

			// Handle special punctuation-initiated constructs
			if pos == len(b)-1 {
				switch b[pos] {
				case '+': // raw matchertext
					return p.rawText()

				case '-': // comment
					return p.comment()
				}
			}

			// Invoke client's handler to parse element
			e = p.handleElement(b[pos:])
			return
		}
	}

	// Otherwise, handle the matchertext pair as literal text
	return p.literalPair(o, c)
}

// Handle an opener/closer pair and its content as literal text.
func (p *Parser) literalPair(o, c byte) (e error) {

	// Suck space since the previous construct and leading up to the opener
	p.suckSpace(o == '[')

	// Buffer the opener and enter the corresponding state
	oPos := p.buf.Len()
	p.buf.WriteByte(o)
	p.sawMatcher(o)

	// Parse matchertext until we see the corresponding closer
	e = p.mp.ReadPair(p.mh, o, c)
	if e != nil {
		return
	}

	// If the pair contained any nested matchertext,
	// then our state will now be a closer instead of an open bracket.
	maybeRef := p.lmb == '['

	// Suck space since last construct and/or leading up to the closer
	maybeRef = !p.suckSpace(c == ']') && maybeRef

	// Buffer the closer and enter the corresponding state
	p.buf.WriteByte(c)
	p.sawMatcher(c)

	// Check for special unmatched-matcher symbolic references
	b := p.buf.Bytes()[oPos:]
	l := len(b)
	if l == 5 && matchertext.IsOpener(b[1]) &&
		(b[2] == '<' || b[2] == '>') {
		maybeRef = true
	}

	// See if the pair represents a character reference.
	if maybeRef {
		if b[0] != '[' || b[l-1] != ']' {
			panic("character reference not bracketed")
		}
		ref := b[1 : l-1]

		// Avoid treating [<] or [>] as a 1-character reference
		if l == 3 && (b[1] == '<' || b[1] == '>') {
			return
		}

		// See if the bracket pair contains a valid reference name.
		if isReference(ref) {

			// Handle normal text before the character reference
			e = p.handleText(oPos, false)
			if e != nil {
				return
			}

			// Consume the character reference text
			p.buf.Reset()
			p.sawMatcher(c)

			// Handle the character reference itself
			e = p.handleReference(ref)
		}
	}
	return
}

// Read a matching bracket pair with the markup handler, then flush the output.
func (p *Parser) mPair(h matchertext.Handler) error {

	p.sawMatcher('[')

	// Read the contents of the bracket pair
	if e := p.mp.ReadPair(h, '[', ']'); e != nil {
		return e
	}

	// Flush any plain text at the end to the text handler
	if e := p.mFlush(true); e != nil {
		return e
	}

	p.sawMatcher(']')
	return nil
}

// Flush any accumulated bytes to the text handler.
// Suck space leading up to the end of the buffer if atEnd is true.
func (p *Parser) mFlush(atEnd bool) error {

	// XXX optionally (based on configuration) check all UTF-8 runes?

	// Suck space after a previous markup construct if appropriate
	p.suckSpace(atEnd)

	// Pass any remaining buffered bytes to the client
	return p.handleText(p.buf.Len(), false)
}

// Set last matcher state appropriately after processing an opener or closer.
func (p *Parser) sawMatcher(b byte) {
	if b == '[' || b == ']' {
		p.lmb = b
		p.lmp = p.buf.Len()
	}
}

// Suck space after the last markup construct, if p.lmb != 0,
// and/or leading up to a subsequent construct, if atEnd is true.
func (p *Parser) suckSpace(atEnd bool) (sucked bool) {

	// First suck space after the last construct if appropriate
	if p.lmb != 0 {
		b := p.buf.Bytes()[p.lmp:]
		l := len(b)
		n := scanPostSpace(b)
		switch {
		case n > 0 && n == l-1 && atEnd && b[n] == '<':
			// Special case: ">...space...<"
			p.buf.Truncate(p.lmp)
			sucked = true

		case n > 0 && p.lmp > 0:
			// Must shift un-sucked content left by n to fill gap
			copy(b, b[n:])
			p.buf.Truncate(p.buf.Len() - n)
			sucked = true

		case n > 0: // p.lmp == 0
			// We can just advance past the sucked text.
			p.buf.Next(n)
			sucked = true
		}
		p.lmb = 0
	}

	// Now suck space leading up to the next matcher if appropriate
	if atEnd {
		b := p.buf.Bytes()
		l := scanPreSpace(b)
		if l < len(b) {
			p.buf.Truncate(l)
			sucked = true
		}
	}
	return
}

// Invoke the client's HandlerText to handle the next n bytes of buffered text.
func (p *Parser) handleText(n int, raw bool) (e error) {

	// Don't call the client's handler if there's no text to handle
	if n == 0 {
		return nil
	}

	// Obtain the byte slice to pass to the client's handler.
	// The slice's contents will be valid only until
	// the next time the handler invokes the parser.
	b := p.buf.Next(n)

	// Save and restore the handlers around the handler upcall,
	// in case the handler recursively invokes parser methods.
	h := p.h
	e = h.t.Text(b, raw)
	p.h = h

	return
}

// Invoke the client's reference to handle a character reference.
// Assumes the matchertext of the reference has already been consumed.
func (p *Parser) handleReference(name []byte) (e error) {

	// Save and restore the handlers around the handler upcall,
	// in case the handler recursively invokes parser methods.
	h := p.h
	e = h.t.Reference(name)
	p.h = h

	return
}

// Invoke the client's element handler to handle a MinML element.
// The handler is normally expected to invoke ReadElement recursively.
func (p *Parser) handleElement(name []byte) (e error) {

	// Save and restore the handlers around the handler upcall.
	h := p.h
	e = h.m.Element(name)
	p.h = h

	return
}

// Read a MinML element starting at the current position.
// The client is expected to call this method from its Element handler.
// The parser uses the provided hm to handle markup contained in this element.
func (p *Parser) ReadElement(name []byte, he HandlerElement) error {

	// Consume the buffered element name
	p.buf.Reset()

	// Stash the provided element handler in our state
	p.h = handlers{e: he}

	// Parse any attributes within curly braces
	b, e := p.mp.PeekByte()
	if e != nil {
		return e
	}
	if b == '{' {

		// Parse the matchertext content of the delimited pair.
		// only while parsing attributes.
		e = p.mp.ReadPair(p.ah, '{', '}')
		if e != nil {
			return e
		}

		// Make sure no partial attribute was accumulated
		e = p.aFlush()
		if e != nil {
			return e
		}
	}

	// Invoke the client's Content handler to parse element content
	return he.Content()
}

// Matchertext handler for element attribute parsing
type aHandler struct {
	p *Parser
}

func (ah aHandler) Byte(b byte) error {
	p := ah.p

	// Flush any previous attribute whenever we encounter whitespace
	if xml.IsSpace(b) {
		return p.aFlush()
	}

	// Accumulate element name bytes in the buffer
	if b != '=' {
		return p.buf.WriteByte(b)
	}

	// Ensure the attribute name is actually a valid XML name
	name := p.buf.Bytes()
	if !xml.IsName(name) {
		return p.syntaxError("invalid attribute name")
	}
	p.buf.Reset() // consume the name

	// Invoke the client to handle the element and parse its value
	h := p.h
	e := h.e.Attribute(name)
	p.h = h

	return e
}

func (ah aHandler) Open(o, c byte) error {
	return ah.p.syntaxError("attribute name expected")
}

// Handle any residual bytes in the buffer while parsing attributes
func (p *Parser) aFlush() error {
	if p.buf.Len() > 0 {
		return p.syntaxError("attribute value expected")
	}
	return nil
}

// Read an element attribute.
// The client is expected to call this method from its Attribute handler.
// The parser uses the provided ht to handle attribute value text.
func (p *Parser) ReadAttribute(name []byte, ht HandlerText) error {

	// Stash the client-provided text handler
	p.h = handlers{t: ht}

	// See if the attribute's value is bracket-quoted.
	b, e := p.mp.PeekByte()
	if e != nil {
		return e
	}
	if b == '[' {
		// Parse the quoted bracket pair
		e = p.mPair(p.mh)
		if e != nil {
			return e
		}

		// Ensure that there's no garbage after the close bracket
		b, e = p.mp.PeekByte()
		if e != nil {
			return e
		}
		if b != '}' && !xml.IsSpace(b) {
			return p.syntaxError("end of attribute value expected")
		}
	} else {
		// Parse unquoted attribute value text
		// until we encounter a space or close brace
		_, e = p.mp.ReadText(p.vh)
		if e == vdone {
			e = nil
		}
		if e != nil {
			return e
		}
	}

	// Flush any plain text at the end to the text handler.
	return p.mFlush(false)
}

// Matchertext handler for parsing unquoted attribute values
type vHandler struct {
	p *Parser
}

func (vh vHandler) Byte(b byte) error {
	p := vh.p

	// The unquoted value ends at the first space (not between matchers)
	if xml.IsSpace(b) {
		return vDone{}
	}

	// Otherwise just accumulate attribute value bytes
	return p.buf.WriteByte(b)
}

// Matcher-delimited sequences within an unquoted value are literal
func (vh vHandler) Open(o, c byte) error {
	return vh.p.literalPair(o, c)
}

// ephemeral "error" that just terminates the parsing of a value
type vDone struct{}

var vdone vDone

func (_ vDone) Error() string {
	return "done parsing attribute value"
}

// Read the content of a MinML element.
// The client is expected to call this method from its Content handler.
// The parser uses the provided ht to handle attribute value text.
func (p *Parser) ReadContent(hm HandlerMarkup) error {

	// Stash the client's parsing handlers in our state
	p.h = handlers{m: hm, t: hm}

	// Parse the matchertext content of the bracket pair
	return p.mPair(p.mh)
}

// Read a raw matchertext construct +[...].
func (p *Parser) rawText() error {

	// Parse and buffer the raw matchertext content between the brackets
	if e := p.mp.ReadPair(p.rh, '[', ']'); e != nil {
		return e
	}

	// Send the collected matchertext to the (raw) text handler
	if e := p.handleText(p.buf.Len(), true); e != nil {
		return e
	}

	p.sawMatcher(']')
	return nil
}

type rHandler struct {
	p *Parser
}

func (rh rHandler) Byte(b byte) error {
	return rh.p.buf.WriteByte(b)
}

func (rh rHandler) Open(o, c byte) (e error) {
	p := rh.p

	// Write the literal opener
	p.buf.WriteByte(o)

	// Parse matchertext until we see the corresponding closer
	e = p.mp.ReadPair(p.rh, o, c)
	if e != nil {
		return
	}

	// Write the literal closer
	p.buf.WriteByte(c)
	return
}

// Read a comment construct -[...]
func (p *Parser) comment() error {

	// Parse and buffer the raw matchertext content between the brackets
	if e := p.mp.ReadPair(p.rh, '[', ']'); e != nil {
		return e
	}

	// Send the comment text to the client's optional comment handler
	if e := p.handleComment(); e != nil {
		return e
	}

	p.sawMatcher(']')
	return nil
}

// Invoke the client's HandlerText to handle the next n bytes of buffered text.
func (p *Parser) handleComment() (e error) {

	// Obtain the byte slice to pass to the client's handler.
	// The slice's contents will be valid only until
	// the next time the handler invokes the parser.
	b := p.buf.Bytes()
	if len(b) == 0 {
		return nil
	}
	p.buf.Reset()

	// Save and restore the handlers around the handler upcall,
	// in case the handler recursively invokes parser methods.
	h := p.h
	if c, ok := h.t.(HandlerComment); ok {
		e = c.Comment(b)
	}
	p.h = h

	return
}

func (p *Parser) syntaxError(msg string) *matchertext.SyntaxError {
	return p.mp.SyntaxError(msg)
}
