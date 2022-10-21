package minml

// This map contains MinML symbolic character reference extensions.
var Entity = map[string]string{

	// Matchertext convenience escapes
	"(<)": "(",
	"(>)": ")",
	"[<]": "[",
	"[>]": "]",
	"{<}": "{",
	"{>}": "}",

	// Punctuation
	//	"-":	"\u00AD",	// soft hyphen?
	"--":  "\u2013", // – en dash
	"---": "\u2014", // — em dash

	// Basic mathematical symbols
	"+-": "\u00B1", // ± plus-minus
	"-+": "\u2213", // ∓ minus-or-plus
	"x":  "\u00D7", // × times
	"d":  "\u00F7", // ÷ division sign
	//	"/":	"\u2044",	// ⁄ fraction slash?
	//	"/":	"\u2215",	// ∕ division slash?
	".":   "\u22C5", // ⋅ dot operator
	":":   "\u2236", // ∶ ratio
	"::":  "\u2237", // ∷ proportion
	"2rt": "\u221A", // √ square root
	"3rt": "\u221B", // ∛ third root
	"4rt": "\u221C", // ∜ fourth root

	// Comparison operators
	"<=":   "\u2264", // ≤ less-than or equal to
	">=":   "\u2265", // ≥ greater-than or equal to
	"<>":   "\u2276", // ≶ less-than or greater-than
	"><":   "\u2277", // ≷ greater-than or less-than
	"<<":   "\u226A", // ≪ much less-than
	">>":   "\u226B", // ≫ much greater-than
	"<<<":  "\u22D8", // ⋘ very much less-than
	">>>":  "\u22D9", // ⋙ very much greater-than
	"~~":   "\u2248", // ≈ almost equal to
	"~~=":  "\u224A", // ≊ almost equal or equal to
	"def=": "\u225D", // ≝ equal to by definition

	// Negated comparison operators
	"/=":  "\u2260", // ≠ not equal to
	"/<":  "\u226E", // ≮ not less than
	"/>":  "\u226F", // ≯ not greater than
	"/<=": "\u2270", // ≰ neither less-than nor equal to
	"/>=": "\u2271", // ≱ neither greater-than nor equal to
	"/<>": "\u2278", // ≸ neither less-than nor greater-than
	"/><": "\u2279", // ≹ neither greater-than nor less-than
	"/~~": "\u2249", // ≉ not almost equal to

	// Common mathematical arrows
	"<--":  "\u2190", // ← left arrow
	"-->":  "\u2192", // → right arrow
	"<->":  "\u2194", // ↔ left right arrow
	"<==":  "\u21D0", // ⇐ leftwards double arrow
	"==>":  "\u21D2", // ⇒ rightwards double arrow
	"<=>":  "\u21D4", // ⇔ left right double arrow
	"<---": "\u27F5", // ⟵ long leftwards arrow
	"--->": "\u27F6", // ⟶ long rightwards arrow
	"<-->": "\u2194", // ⟷ long left right arrow
	"<===": "\u27F8", // ⟸ long leftwards double arrow
	"===>": "\u27F9", // ⟹ long rightwards double arrow
	"<==>": "\u27FA", // ⟺ long left right double arrow

	// Arrows with stroke
	"/<--": "\u219A", // ↚ leftwards arrow with stroke
	"/-->": "\u219B", // ↛ rightwards arrow with stroke
	"/<->": "\u21AE", // ↮ left right arrow with stroke
	"/<==": "\u21CD", // ⇐ leftwards double arrow with stroke
	"/==>": "\u21CF", // ⇏ rightwards double arrow with stroke
	"/<=>": "\u21CE", // ⇎ left right double arrow with stroke

	// Tacks
	"|--": "\u22A2", // ⊢ right tack (proves)
	"--|": "\u22A3", // ⊣ left tack
	"~|~": "\u22A4", // ⊤ down tack (bottom)
	"_|_": "\u22A5", // ⊥ up tack (top)
	"|-":  "\u22A6", // ⊦ assertion
	"|=":  "\u22A7", // ⊧ models
	"|==": "\u22A8", // ⊨ true
	"||-": "\u22A9", // ⊩ forces
	"||=": "\u22AB", // ⊫

	// Tacks with stroke
	"/|--": "\u22AC", // ⊬ does not prove
	"/|==": "\u22AD", // ⊭ not true
	"/||-": "\u22AE", // ⊮ does not force
	"/||=": "\u22AF", // ⊯

	// Logical operators
	"-.": "\u00AC", // ¬ logical not
	"^":  "\u2227", // ∧ logical and
	"v":  "\u2228", // ∨ logical or
	"v-": "\u22BB", // ⊻ logical xor
	"-^": "\u22BC", // ⊼ logical nand
	"-v": "\u22BD", // ⊽ logical nor

	// Vulgar fractions
	"1/4":  "\u00BC", // ¼
	"1/2":  "\u00BD", // ½
	"3/4":  "\u00BE", // ¾
	"1/7":  "\u2150", // ⅐
	"1/9":  "\u2151", // ⅑
	"1/10": "\u2152", // ⅒
	"1/3":  "\u2153", // ⅓
	"2/3":  "\u2154", // ⅔
	"1/5":  "\u2155", // ⅕
	"2/5":  "\u2156", // ⅖
	"3/5":  "\u2157", // ⅗
	"4/5":  "\u2158", // ⅘
	"1/6":  "\u2159", // ⅙
	"5/6":  "\u215A", // ⅚
	"1/8":  "\u215B", // ⅛
	"3/8":  "\u215C", // ⅜
	"5/8":  "\u215D", // ⅝
	"7/8":  "\u215E", // ⅞
}
