package matchertext

import (
	"slices"
	"strings"
	"testing"
)

type pairHandler struct{ p *Parser }

func (h pairHandler) Byte(byte) error      { return nil }
func (h pairHandler) Open(o, c byte) error { return h.p.ReadPair(h, o, c) }

func TestRecovery(t *testing.T) {
	for _, c := range []struct {
		s   string
		ofs []int64 // offsets of the reported errors
	}{
		{"a(b)c", nil},
		{"a]b", []int64{1}},
		{"x(a", []int64{1}},
		{"[(a]", []int64{3}},
		{"(a]b)", []int64{2, 2, 4}},
		{"{[}", []int64{2}},
	} {
		var ofs []int64
		p := NewParser(strings.NewReader(c.s))
		p.HandleError = func(err error) error {
			ofs = append(ofs, err.(*SyntaxError).Offset())
			return nil
		}
		if err := p.ReadAll(pairHandler{p}); err != nil {
			t.Errorf("%q: %v", c.s, err)
		}
		if !slices.Equal(ofs, c.ofs) {
			t.Errorf("%q: errors at %v, want %v", c.s, ofs, c.ofs)
		}
	}
}
