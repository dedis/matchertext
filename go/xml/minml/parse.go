package minml

import (
	"bytes"
	"io"
	"unicode/utf8"

	"github.com/dedis/matchertext.git/go/xml/matchertext"
	"github.com/dedis/matchertext.git/go/xml/syntax"
)

// HandlerMarkup represents client logic for handling general XML markup,
// which may contain elements, text, and character references.
// This interface extends HandlerText for handling text and references.
//
// The parser calls Element whenever it encounters an XML element.
// The handler must invoke the provided parseElement function exactly once
// when it is ready to parse the element's attributes and/or content.
type HandlerMarkup interface {
	HandlerText
	Element(name []byte) error
}

// HandlerText represents client logic for handling text markup,
// which may contain plain text and character references.
// The parser will invoke Text for each contiguous sequence of normal text,
// and will invoke Reference on encountering a character reference.
type HandlerText interface {
	Text(text []byte) error      // Handle plain UTF-8 text
	Reference(name []byte) error // Handle a character reference
}

// HandlerElement represents client logic for handling an XML element.
// While parsing an element, the parser
// will invoke Attribute one per attribute if the element has attributes,
// then the parser will invoke Content exactly once for this element
// (unless a parse error occurs before getting to the element content).
type HandlerElement interface {
	Attribute(name []byte) error
	Content() error
}

// The above three handler interfaces bundled into one struct
type handlers struct {
	m HandlerMarkup
	t HandlerText
	e HandlerElement
}

// Create a new Parser that reads MinML input from r.
func NewParser(r io.Reader) *Parser {
	p := &Parser{}
	p.SetReader(r)
	return p
}

type Parser struct {
	mp matchertext.Parser

	mh mHandler // matchertext callback for markup parsing
	ah aHandler // matchertext callback for attribute parsing
	vh vHandler // matchertext callback for unquoted value parsing

	buf   bytes.Buffer // bytes read so far but not yet consumed
	state byte         // last markup matcher seen
	err   error

	// markup handlers to use while parsing matchertext
	h handlers
}

func (p *Parser) init() {

	// Initialize back pointers from the matchertext parsing handlers
	p.mh.p = p
	p.ah.p = p
	p.vh.p = p

	// Clear the parsing state
	p.buf.Reset()
	p.state = 0
	p.err = nil
	p.h = handlers{}
}

func (p *Parser) SetReader(r io.Reader) {
	p.init()
	p.mp.SetReader(r)
}

func (p *Parser) SetByteReader(br io.ByteReader) {
	p.init()
	p.mp.SetByteReader(br)
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
	if _, e := p.mp.ReadText(p.mh); e != nil {
		return e
	}

	// Flush any plain text at the end to the text handler
	return p.mFlush()
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
func (mh mHandler) Open(o, c byte) error {
	p := mh.p

	// If we just processed the attributes in an element start,
	// make sure it's followed by bracketed content as required.
	//	if p.state == '{' {
	//		if o != '[' {
	//			return syntaxError("open bracket expected")
	//		}
	//		return p.markupContent(hm, mp)
	//	}

	// First suck any space after the previous construct if appropriate
	p.suckPostSpace()

	// Check for elements only when we see bracket or brace openers,
	// and only if we're parsing general rather than text-only markup.
	if o != '(' && p.h.m != nil {

		// See if the opener is the start of a markup element.
		b := p.buf.Bytes()
		if preLen, name := scanElementStart(b); name != nil {

			// Handle any character data preceding the element
			if e := p.handleText(preLen); e != nil {
				return e
			}

			// Invoke the client's handler to parse the element
			e := p.handleElement(name)
			return e
		}
	}

	// Otherwise, handle the matchertext pair as literal text
	return p.literalPair(o, c)
}

// Handle an opener/closer pair and its content as literal text.
func (p *Parser) literalPair(o, c byte) (e error) {

	// First suck any space after the previous construct if appropriate
	p.suckPostSpace()

	// Parentheses parsing doesn't do space sucking
	oState, cState := o, c
	if o == '(' {
		oState, cState = 0, 0
	}

	// Suck space leading up to the opener
	p.suckPreSpace(o)

	// Buffer the opener and enter the corresponding state
	oPos := p.buf.Len()
	p.buf.WriteByte(o)
	p.state = oState

	// Parse matchertext until we see the corresponding closer
	e = p.mp.ReadPair(p.mh, o, c)
	if e != nil {
		return
	}

	// If the pair contained any nested matchertext,
	// then our state will now be a closer instead of an open bracket.
	maybeRef := p.state == '['

	// Suck any space after the previous construct if appropriate
	maybeRef = !p.suckPostSpace() && maybeRef

	// Suck space leading up to the closer
	maybeRef = !p.suckPreSpace(o) && maybeRef

	// Buffer the closer and enter the corresponding state
	p.buf.WriteByte(c)
	p.state = cState

	// See if the pair represents a character reference.
	if maybeRef {
		b := p.buf.Bytes()[oPos:]
		l := len(b)
		if b[0] != '[' || b[l-1] != ']' {
			panic("character reference not bracketed")
		}
		name := b[1 : l-1]

		// If the bracket pair contained nothing but an XML name,
		// then handle it as a character reference.
		if syntax.IsName(name) {

			// Handle normal text before the character reference
			e = p.handleText(oPos)
			if e != nil {
				return
			}

			// Handle the character reference itself
			e = p.handleReference(name)
		}
	}
	return
}

// Flush any accumulated bytes to the text handler
func (p *Parser) mFlush() error {

	// XXX optionally (based on configuration) check all UTF-8 runes?

	// Suck space after a previous markup construct if appropriate
	p.suckPostSpace()

	// Pass any remaining buffered bytes to the client
	return p.handleText(p.buf.Len())
}

// If the buffered bytes immediately follow a markup construct
// (element or reference), scan for a following space sucker.
// Returns true if any space was sucked.
func (p *Parser) suckPostSpace() (sucked bool) {

	// If state != 0, then the currently buffered text
	// immediately follows an open or close bracket or brace.
	if p.state != 0 {
		n := scanPostSpace(p.buf.Bytes())
		p.buf.Next(n)
		sucked = n > 0
		p.state = 0
	}
	return
}

// Scan for a space sucker just before an open matcher.
func (p *Parser) suckPreSpace(o byte) (sucked bool) {
	if o != '(' {
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
func (p *Parser) handleText(n int) (e error) {

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
	e = h.t.Text(b)
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

	// Make sure we're actually at the start of a markup element.
	//	preLen, name := scanElementStart(p.buf.Bytes())
	//	if preLen != 0 || name == nil {
	//		return p.syntaxError("markup element expected")
	//	}

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
		p.state = '{'
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
	if syntax.IsSpace(b) {
		return p.aFlush()
	}

	// Accumulate element name bytes in the buffer
	if b != '=' {
		return p.buf.WriteByte(b)
	}

	// Ensure the attribute name is actually a valid XML name
	name := p.buf.Bytes()
	if !syntax.IsName(name) {
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
		e = p.mp.ReadPair(p.mh, '[', ']')
		if e != nil {
			return e
		}

		// Ensure that there's no garbage after the close bracket
		b, e = p.mp.PeekByte()
		if e != nil {
			return e
		}
		if b != '}' && !syntax.IsSpace(b) {
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

	// Flush any plain text at the end to the text handler
	return p.mFlush()
}

// Matchertext handler for parsing unquoted attribute values
type vHandler struct {
	p *Parser
}

func (vh vHandler) Byte(b byte) error {
	p := vh.p

	// The unquoted value ends at the first space (not between matchers)
	if syntax.IsSpace(b) {
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

	p.state = '['

	// Parse the matchertext content of the bracket pair
	if e := p.mp.ReadPair(p.mh, '[', ']'); e != nil {
		return e
	}

	// Flush any plain text at the end to the text handler
	if e := p.mFlush(); e != nil {
		return e
	}

	// Suck space before the close bracket
	p.suckPreSpace('[')

	p.state = ']'
	return nil
}

// Scan the buffered text, leading up to an open bracket or brace,
// for an XML element name and optional sucked whitespace preceding it.
// Returns the length of unconsumed text preceding the element name if any,
// and the name if there is one or nil if none.
func scanElementStart(b []byte) (preLen int, name []byte) {

	// Scan for the first NameStartChar in a sequence of NameChars.
	n := -1
	for l := len(b); l > 0; {
		r, size := utf8.DecodeLastRune(b[:l])
		if r == utf8.RuneError && size == 1 {
			break
		}
		l -= size
		if syntax.IsNameStartChar(r) {
			n = l
		}
		if !syntax.IsNameChar(r) {
			break
		}
	}
	if n < 0 {
		return len(b), nil // no immediately-preceding name
	}

	// Return the name and preceding text before any sucked space
	return scanPreSpace(b[:n]), b[n:]
}

// Scan for an optional space-sucker '<' and whitespace
// immediately preceding markup (an element or  reference).
// Returns len(b) or the position at which sucked whitespace starts.
func scanPreSpace(b []byte) int {
	l := len(b)
	if l > 0 && b[l-1] == '<' {
		for l--; l > 0 && syntax.IsSpace(b[l-1]); l-- {
		}
	}
	return l
}

// Scan for an optional space-sucker '>' and whitespace
// immediately following markup (an element or reference).
// Returns the number of prefix bytes of b that should be dropped.
func scanPostSpace(b []byte) int {
	l := 0
	if len(b) > 0 && b[0] == '>' {
		for l++; l < len(b) && syntax.IsSpace(b[l]); l++ {
		}
	}
	return l
}

// Returns true if b contains a valid XML name.
//func isName(b []byte) bool {
//	for i, r := range m {
//		if i == 0 && !syntax.IsNameStartChar(r) {
//			return "", m
//		}
//		if i > 0 && !syntax.IsNameChar(r) {
//			break
//		}
//	}
//	return
//}


func (p *Parser) syntaxError(msg string) *matchertext.SyntaxError {
	return p.mp.SyntaxError(msg)
}

