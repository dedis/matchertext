package matchertext

// IsMatcher returns true if b is any ASCII matcher character:
// parentheses (), square brackets [], or curly braces {}.
func IsMatcher(b byte) bool {
	return IsOpener(b) || IsCloser(b)
}

// IsOpener returns true if b is an open parenthesis,
// square bracket, or curly brace.
func IsOpener(b byte) bool {
	return b == '(' || b == '[' || b == '{'
}

// IsCloser returns true if b is a close parenthesis,
// square bracket, or curly brace.
func IsCloser(b byte) bool {
	return b == ')' || b == ']' || b == '}'
}

// IsMatched returns true if o is an opener and c is the matching closer
func IsMatched(o, c byte) bool {
	return (o == '(' && c == ')') ||
		(o == '[' && c == ']') ||
		(o == '{' && c == '}')
}
