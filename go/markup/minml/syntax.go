package minml

import (
	"github.com/dedis/matchertext/go/markup/xml"
	"github.com/dedis/matchertext/go/matchertext"
)

// Scan the buffered text for the start of an element name
// leading up to an open bracket or curly brace.
// Returns the position of the name, or -1 if none found.
func scanStarter(b []byte) int {

	// Scan for the first space preceding the open matcher.
	n := -1
	for i := len(b) - 1; i >= 0 && isNameByte(b[i]); i-- {
		n = i
	}

	// Avoid pulling a left space sucker into the element name
	if n >= 0 && b[n] == '<' {
		if n == len(b)-1 {
			return -1 // only a space sucker, no element name
		}
		return n + 1 // element name starts after space sucker
	}

	return n
}

// Scan for an optional space-sucker '<' and whitespace
// immediately preceding markup (an element or  reference).
// Returns len(b) or the position at which sucked whitespace starts.
func scanPreSpace(b []byte) int {

	// Scan backwards to suck space
	l := len(b)
	if l >= 2 && b[l-1] == '<' && xml.IsSpace(b[l-2]) {
		for l -= 2; l > 0 && xml.IsSpace(b[l-1]); l-- {
		}
	}

	return l
}

// Scan for an optional space-sucker '>' and whitespace
// immediately following markup (an element or reference).
// Returns the number of prefix bytes of b that should be dropped.
func scanPostSpace(b []byte) int {
	l := 0
	if len(b) >= 2 && b[0] == '>' && xml.IsSpace(b[1]) {
		for l += 2; l < len(b) && xml.IsSpace(b[l]); l++ {
		}
	}
	return l
}

// Return true if b can be within a liberalized MinML element name.
// MinML allows punctuation: anything but XML whitespace and matchers.
func isNameByte(b byte) bool {
	return !xml.IsSpace(b) && !matchertext.IsMatcher(b)
}

// Return true if slice b can be a liberalized MinML reference.
// MinML allows punctuation: anything but XML whitespace, even matchers.
func isReference(b []byte) bool {
	if len(b) == 0 {
		return false
	}
	for i := 0; i < len(b); i++ {
		if xml.IsSpace(b[i]) {
			return false
		}
	}
	return true
}
