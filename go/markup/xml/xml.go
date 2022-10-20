// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package xml implements a simple XML 1.0 parser that
// understands XML name spaces.
package xml

// References:
//    Annotated XML spec: https://www.xml.com/axml/testaxml.htm
//    XML name spaces: https://www.w3.org/TR/REC-xml-names/

import (
	"unicode"
	"unicode/utf8"
)

// Decide whether the given rune is in the XML Character Range, per
// the Char production of https://www.w3.org/TR/xml/#charsets
// in Section 2.2 Characters.
func IsChar(r rune) (inrange bool) {
	return r == 0x09 ||
		r == 0x0A ||
		r == 0x0D ||
		r >= 0x20 && r <= 0xD7FF ||
		r >= 0xE000 && r <= 0xFFFD ||
		r >= 0x10000 && r <= 0x10FFFF
}

// Returns true if b is a valid whitespace character in XML.
func IsSpace(b byte) bool {
	return b == ' ' || b == '\r' || b == '\n' || b == '\t'
}

// Returns true if r is a NameStartChar that can begin an XML name.
func IsNameStartChar(r rune) bool {
	return unicode.Is(first, r)
}

// Returns true if r is a NameChar that can be part of an XML name.
func IsNameChar(r rune) bool {
	return unicode.Is(first, r) || unicode.Is(second, r)
}

// IsName returns true if byte slice s contains a valid XML Name.
func IsName(s []byte) bool {
	if len(s) == 0 {
		return false
	}
	c, n := utf8.DecodeRune(s)
	if c == utf8.RuneError && n == 1 {
		return false
	}
	if !unicode.Is(first, c) {
		return false
	}
	for n < len(s) {
		s = s[n:]
		c, n = utf8.DecodeRune(s)
		if c == utf8.RuneError && n == 1 {
			return false
		}
		if !unicode.Is(first, c) && !unicode.Is(second, c) {
			return false
		}
	}
	return true
}

// IsNameString returns true if string s contains a valid XML Name.
// XXX need?
func isNameString(s string) bool {
	if len(s) == 0 {
		return false
	}
	c, n := utf8.DecodeRuneInString(s)
	if c == utf8.RuneError && n == 1 {
		return false
	}
	if !unicode.Is(first, c) {
		return false
	}
	for n < len(s) {
		s = s[n:]
		c, n = utf8.DecodeRuneInString(s)
		if c == utf8.RuneError && n == 1 {
			return false
		}
		if !unicode.Is(first, c) && !unicode.Is(second, c) {
			return false
		}
	}
	return true
}

// These tables are derived from the XML specification 1.0 (Fifth Edition) 2008
// at https://www.w3.org/TR/xml/#sec-common-syn
// First corresponds to NameStartChar
// and second corresponds to NameChar \ NameStartChar.

var first = &unicode.RangeTable{
	R16: []unicode.Range16{
		{0x003A, 0x003A, 1}, // :
		{0x0041, 0x005A, 1}, // A-Z
		{0x005F, 0x005F, 1}, // _
		{0x0061, 0x007A, 1}, // a-z
		{0x00C0, 0x00D6, 1},
		{0x00D8, 0x00F6, 1},
		{0x00F8, 0x02FF, 1},
		{0x0370, 0x037D, 1},
		{0x037F, 0x1FFF, 1},
		{0x200C, 0x200D, 1},
		{0x2070, 0x218F, 1},
		{0x2C00, 0x2FEF, 1},
		{0x3001, 0xD7FF, 1},
		{0xF900, 0xFDCF, 1},
		{0xFDF0, 0xFFFD, 1},
	},
}

var second = &unicode.RangeTable{
	R16: []unicode.Range16{
		{0x002D, 0x002E, 1}, // -.
		{0x0030, 0x0039, 1}, // 0-9
		{0x00B7, 0x00B7, 1}, // ·
		{0x0300, 0x036F, 1},
		{0x203F, 0x2040, 1},
	},
}

// IsReference returns true if s is a valid character reference name or number:
// i.e., either an XML name, or #[0-9]+, or #x[0-9a-fA-F]+.
func IsReference(s []byte) bool {
	l := len(s)

	// If it doesn't start with #, then just see if it's a name.
	if l == 0 || s[0] != '#' {
		return IsName(s)
	}

	// Is it a hex name with at least one digit?
	if l > 2 && s[1] == 'x' {
		for i := 2; i < l; i++ {
			b := s[i]
			if (b < '0' || b > '9') &&
				(b < 'a' || b > 'f') &&
				(b < 'A' || b > 'F') {
				return false
			}
		}
		return true
	}

	// Is it a decimal name with at least one digit?
	if l > 1 {
		for i := 1; i < l; i++ {
			b := s[i]
			if b < '0' || b > '9' {
				return false
			}
		}
		return true
	}

	return false
}
