package matchertext

import (
	"io"
	"sort"

	"github.com/dedis/matchertext/go/internal/util"
)

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
// Maybe just a general streaming escaper with a
// customizable dictionary of escape codes would be more useful.
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

// Unmatched reads io.Reader r and returns
// a slice listing the byte offsets in r, if any,
// at which unmatched matchers appear.
// The returned list is not necessarily sorted,
// but sort.Sort() may be used to sort the resulting slice if needed.
// Returns a nil OffsetSlice if input r is valid matchertext.
// Returns an error only if an I/O error occurs while reading r.
func UnmatchedOffsets(r io.Reader) (OffsetSlice, error) {
	br := util.ToByteScanner(r)

	// Scan the input stream until we reach io.EOF or another I/O error
	_, os, err := unmatchedScan(br, true, 0, nil)
	if err != io.EOF {
		return os, err
	}
	return os, nil // successful completion
}

func unmatchedScan(br io.ByteScanner, all bool, ofs int64, os OffsetSlice) (
	int64, OffsetSlice, error) {

	for {
		b, err := br.ReadByte()
		if err != nil {
			return ofs, os, err
		}

		switch {
		case IsOpener(b):
			o := b

			// Recursively scan the contents of the matcher pair
			newOfs, newOs, err := unmatchedScan(
				br, false, ofs+1, os)

			// Scan the hopefully-matching closer
			if err == nil {
				b, err = br.ReadByte()
			}

			// Check that the opener is properly closed
			switch {
			case err == io.EOF:
				newOs = append(newOs, ofs) // unmatched opener
				err = nil                  // consume error

			case err != nil: // other I/O errors are fatal
				return newOfs, newOs, err

			case IsMatched(o, b):
				newOfs++ // account for closer

			default:
				newOs = append(newOs, ofs) // unmatched opener
				err = br.UnreadByte()      // don't eat closer
			}
			ofs, os = newOfs, newOs

		case IsCloser(b) && all:
			// Record but then scan past this unmatched closer
			os = append(os, ofs)
			ofs++

		case IsCloser(b):
			// Stop and return before this unmatched closer
			err = br.UnreadByte()
			return ofs, os, err

		default:
			// Scan past non-matchers
			ofs++
		}
	}
}

// OffsetSlice is a slice of int64 offsets in a byte stream
// suitable for passing to sort.Sort().
type OffsetSlice []int64

func (x OffsetSlice) Len() int           { return len(x) }
func (x OffsetSlice) Less(i, j int) bool { return x[i] < x[j] }
func (x OffsetSlice) Swap(i, j int)      { x[i], x[j] = x[j], x[i] }
func (x OffsetSlice) Sort()              { sort.Sort(x) }
