#!/bin/sh
#
# This script converts the JSON version
# of the standard HTML character entity reference table
# to Go code representing the htmlEntity map.

cat >entities.go << EOM
// This file is automatically generated by entities.sh.  DO NOT EDIT.

package html

// HTMLEntity is a map translating from standard HTML entity names
// to corresponding UTF-8 character sequences.
//
var Entity = map[string]string{
EOM

# This script filters out the "compatibility" HTML character entities 
# that don't end in semicolons - those shouldn't appear in XML anyway.

# It also has a horrible hack to convert a few characters from the
# supplementary multilingual plane, which are UTF-16 surrogate pairs in JSON
# but need to be converted to 8-hex-digit notation for Go source.
# The hack only works for the couple such code pages used so far
# in HTML-defined character entities; this might need expanding later.

grep -e '[ ]*\"\&[A-Za-z0-9]*\;\"' entities.json | \
	sed -e 's/  \"\&/\t\"/g' \
	    -e 's/\;\"\:.*\"\\u/\": \"\\u/g' \
	    -e 's/\" \},*$/\",/g' \
	    -e 's/\\uD835\\uDC/\\U0001D4/g' \
	    -e 's/\\uD835\\uDD/\\U0001D5/g' \
	>>entities.go

cat >>entities.go << EOM
}
EOM

gofmt -w entities.go

