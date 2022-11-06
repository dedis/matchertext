package matchertext

import (
	"strings"
	"testing"
)

type unmatchedTest struct {
	s  string
	os OffsetSlice
}

var unmatchedTests = []unmatchedTest{
	{"", nil},
	{"()[]{}", nil},
	{"([{}])", nil},
	{"a(b[c{d}e]f)g", nil},
	{"([{([{}])}])", nil},

	{"(", OffsetSlice{0}},
	{"[", OffsetSlice{0}},
	{"{", OffsetSlice{0}},
	{")", OffsetSlice{0}},
	{"]", OffsetSlice{0}},
	{"}", OffsetSlice{0}},
	{"[{})", OffsetSlice{0, 3}},
	{"a[b{c}d)e", OffsetSlice{1, 7}},
	{")}]({[", OffsetSlice{0, 1, 2, 3, 4, 5}},
	{"[{}({}[())])", OffsetSlice{6, 11}},
	{"([{)}}]])", OffsetSlice{1, 2, 4, 5, 6, 7, 8}},
}

func TestUnmatchedOffsets(t *testing.T) {
	for i, ut := range unmatchedTests {
		r := strings.NewReader(ut.s)
		os, err := UnmatchedOffsets(r)
		if err != nil {
			t.Errorf("%v error: %v", i, err.Error())
			continue
		}

		os.Sort()
		if !eqOffsetSlice(os, ut.os) {
			t.Errorf("%v expecting %v got %v", i, ut.os, os)
		}
	}
}

func eqOffsetSlice(a, b OffsetSlice) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
