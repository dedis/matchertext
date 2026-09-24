package lsp

import (
	"io"
	"sort"
)

// reparse returns the syntax of text, which is the text old was parsed from
// with the bytes [s, e) replaced by n new bytes.
//
// It parses a region from the last restart point before s
// up to the first restart point at or after the edit that the old parse also had,
// under the same parent, and keeps the old results outside the region.
// If the region runs out of its parent's content before that, it retries from the parent;
// a parent precedes its children in Syncs, so the retries end at the top level.
func reparse(old *Syntax, text string, s, e, n int) Syntax {
	delta := n - (e - s)
	newEnd := s + n
	// The construct before a restart point reads the point's first byte, so the edit must not touch it.
	i := sort.Search(len(old.Syncs), func(i int) bool { return old.Syncs[i].Start >= s }) - 1
	for {
		// Region start, its parent, index of its first sync, and number of problems before it
		start, parent, first, probs := 0, -1, 0, 0
		if i >= 0 {
			start, parent, first, probs = old.Syncs[i].Start, old.Syncs[i].Parent, i, old.Syncs[i].Problems
		}
		r := newRecorder(text, start, parent, first, probs)
		next := len(old.Syncs) // index of the old sync where the region stopped
		r.stop = func(q, p int) bool {
			if p != parent || q < newEnd {
				return false
			}
			j := sort.Search(len(old.Syncs), func(j int) bool { return old.Syncs[j].Start >= q-delta })
			if j == len(old.Syncs) || old.Syncs[j].Start != q-delta || old.Syncs[j].Parent != parent {
				return false
			}
			next = j
			return true
		}

		var err error
		if parent < 0 {
			err = r.p.ReadAll(r)
		} else {
			err = r.p.ReadMarkup(r)
		}
		switch {
		case err == errStop:
			return splice(old, &r.syn, start, old.Syncs[next].Start, delta, first, next, probs)
		case parent < 0 && err == nil:
			// The region reached the end of the text: nothing old remains after it.
			return splice(old, &r.syn, start, int(^uint(0)>>1), delta, first, len(old.Syncs), probs)
		case parent >= 0 && (err == nil || err == io.EOF):
			// The region ended its parent's content or reached the end of the text.
			i = parent
		default:
			panic(err)
		}
	}
}

// splice combines the old results before offset start, the region's results,
// and the old results from old offset q on, shifted by delta.
// Old syncs [first, next) are the ones the region replaces,
// and p0 old problems were reported before the region.
func splice(old, reg *Syntax, start, q, delta, first, next, p0 int) Syntax {
	var syn Syntax

	a := sort.Search(len(old.Marks), func(i int) bool { return old.Marks[i].Start >= start })
	b := sort.Search(len(old.Marks), func(i int) bool { return old.Marks[i].Start >= q })
	syn.Marks = make([]Mark, 0, a+len(reg.Marks)+len(old.Marks)-b)
	syn.Marks = append(append(syn.Marks, old.Marks[:a]...), reg.Marks...)
	for _, m := range old.Marks[b:] {
		syn.Marks = append(syn.Marks, Mark{m.Start + delta, m.End + delta, m.Kind})
	}

	a = sort.Search(len(old.Blocks), func(i int) bool { return old.Blocks[i].Open >= start })
	b = sort.Search(len(old.Blocks), func(i int) bool { return old.Blocks[i].Open >= q })
	syn.Blocks = make([]AttrBlock, 0, a+len(reg.Blocks)+len(old.Blocks)-b)
	syn.Blocks = append(append(syn.Blocks, old.Blocks[:a]...), reg.Blocks...)
	n := 0
	for _, k := range old.Blocks[b:] {
		n += len(k.Values)
	}
	values := make([][2]int, 0, n) // one backing array for the shifted values of all blocks
	for _, k := range old.Blocks[b:] {
		from := len(values)
		for _, v := range k.Values {
			values = append(values, [2]int{v[0] + delta, v[1] + delta})
		}
		k.Open += delta
		k.Close += delta
		k.Values = nil // as a fresh parse leaves a block without attributes
		if len(values) > from {
			k.Values = values[from:len(values):len(values)]
		}
		syn.Blocks = append(syn.Blocks, k)
	}

	// Problems split by the order the parser reported them: the construct before a restart point
	// can report a problem at the restart point itself.
	// Old problems reported after the region either follow it or concern an enclosing opener.
	p1 := len(old.Problems)
	if next < len(old.Syncs) {
		p1 = old.Syncs[next].Problems
	}
	syn.Problems = make([]Problem, 0, p0+len(reg.Problems)+len(old.Problems)-p1)
	syn.Problems = append(append(syn.Problems, old.Problems[:p0]...), reg.Problems...)
	for _, p := range old.Problems[p1:] {
		if p.Offset >= q {
			p.Offset += delta
			if p.Hint {
				p.End += delta
			}
		}
		syn.Problems = append(syn.Problems, p)
	}
	addedProbs := p0 + len(reg.Problems) - p1

	added := len(reg.Syncs) - (next - first)
	syn.Syncs = make([]Sync, 0, len(old.Syncs)+added)
	syn.Syncs = append(append(syn.Syncs, old.Syncs[:first]...), reg.Syncs...)
	for _, y := range old.Syncs[next:] {
		if y.Parent >= next {
			y.Parent += added
		} else if y.Parent >= first {
			panic("lsp: restart point outside the region has a parent inside it")
		}
		syn.Syncs = append(syn.Syncs, Sync{y.Start + delta, y.Parent, y.Problems + addedProbs})
	}
	return syn
}

// spliceLines returns the line starts of text, which is the text with line starts old
// with the bytes [s, e) replaced by n new bytes.
func spliceLines(old []int, text string, s, e, n int) []int {
	// A terminator at s-1 can change: "\r" becomes part of "\r\n" if the new text starts with "\n".
	from := max(s-1, 0)
	to := min(s+n+1, len(text)) // a "\r" just before the old e can change the same way
	a := sort.SearchInts(old, from+1)
	b := sort.SearchInts(old, e+2)
	lines := make([]int, 0, len(old)+n/32)
	lines = append(lines, old[:a]...)
	for i := from; i < to; i++ {
		if isLineEnd(text, i) {
			lines = append(lines, i+1)
		}
	}
	for _, l := range old[b:] {
		lines = append(lines, l+n-(e-s))
	}
	return lines
}
