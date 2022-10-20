// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package xml

import (
	"testing"
	"unicode/utf8"
)

func TestIsChar(t *testing.T) {
	invalid := []rune{
		utf8.MaxRune + 1,
		0xD800, // surrogate min
		0xDFFF, // surrogate max
		-1,
	}
	for _, r := range invalid {
		if IsChar(r) {
			t.Errorf("rune %U considered valid", r)
		}
	}
}
