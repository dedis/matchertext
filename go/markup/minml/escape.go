package minml

// Escaper is a configuration bitmask determining
// how to escape MinML text when writing to an output stream.
type escaper int

const (
	escUnmatched escaper = 1 << iota // Escape unmatched matchers XXX
	escReference                     // Escape [ref] as [> ref <]
	escElement                       // Escape tag[...] as tag <[...]

	escMarkup = escReference | escElement // escaping in general markup
	escValue  = escReference              // escaping in attribute values
)

//func (e Escaper) WriteBytesTo(w io.Writer, s []byte) error {
//}
