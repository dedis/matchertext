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
