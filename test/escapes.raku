#!/opt/local/bin/raku

# Verify: changing \o[0] to \o[] causes a syntax error,
# either in the string literal or in the regular expression.
# Thus, introducing \o{} as a matcher escape cannot affect existing code.
my $str = "\o[0]";
if ($str ~~ /\o[0]/) {
	print "NUL matched\n";
}

# Verify: changing \c[0] to \c[] causes a syntax error,
# either in the string literal or in the regular expression.
# Thus, introducing \c{} as a matcher escape cannot affect existing code.
$str = "\c[0]";
if ($str ~~ /\c[0]/) {
	print "NUL matched\n";
}

