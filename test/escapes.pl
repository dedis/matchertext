#!/usr/bin/perl

# Verify: changing \o{0} to \o{} causes a syntax error,
# either in the string literal or in the regular expression.
# Thus, introducing \o{} as a matcher escape cannot affect existing Perl code.
$str = "\o{0}";
if ($str =~ /\o{0}/) {
	print "NUL matched\n";
}

