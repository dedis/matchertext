package matchertext

// Unmatched sets dst to a byte-mask slice the same length as src,
// containing only unmatched matchers in src at corresponding positions.
// Nonmatchers and matched matchers in src become zero bytes in dst.
// If dst is nil or too small, allocates and returns a new slice.
//
// If the unmatched matchers in src - identified by nonzero bytes in dst -
// are erased or replaced with matchertext-compliant escapes,
// then the result will be valid matchertext.
//
// XXX not sure yet if this is really useful/needed.
func notyetUnmatched(dst, src []byte) []byte {
	if len(dst) < len(src) {
		dst = make([]byte, len(src))
	}

	// scan src for unmatched matchers, forming byte mask in dst
	d, s := dst, src
	for len(s) > 0 {
		d, s = unmatched(d, s)
		if len(s) > 0 { // terminated by unmatched closer
			d[0] = s[0]
			s, d = d[1:], s[1:]
		}
	}

	return dst
}

// scan for unmatched matchers in src until the first unmatched closer,
// returning the remainder of the dst and src slices starting at that point.
func unmatched(d, s []byte) (rd, rs []byte) {
	for len(s) > 0 {
		b := s[0]
		if IsOpener(b) {
			rd, rs := unmatched(d[1:], s[1:])
			if len(rs) > 0 && IsMatched(b, rs[0]) { // matched pair
				d[0], rd[0] = 0, 0
				d, s = rd[1:], rs[1:]
			} else {
				d[0] = b // unmatched opener
				d, s = rd, rs
			}
		} else if IsCloser(b) {
			return d, s // unmatched closer
		} else {
			d[0] = 0 // nonmatcher
			d, s = d[1:], s[1:]
		}
	}
	return d, s
}
