// Comments that directly follow a string literal must still be extracted.
const char *a = "a" /* after a literal */;
const char *b = "b" "c" // after adjacent literals
    ;
const char *c = "d"/* no space */;
