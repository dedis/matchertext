//
// string_literal_decode_test.cpp
// Author: Antoine Bastide
// Date: 12.05.2026
//
// Unit tests for Parser::CountRawStringToothpicks — given the *body* of a C/C++
// string literal (the text between the quotes, line splices already removed), it
// returns how many toothpicks survive a rewrite to a raw string literal: \\
// leaves one backslash, \n, \t, and quote escapes are removed, simple escapes for
// control characters (\r, \a, ...) keep a toothpick, and encoding escapes (\x41,
// \101, \u0041, \N{...}) stay as written and are not counted, before or after.
//
// Test inputs are written as raw string literals R"(...)" so the test's own
// compiler does not pre-decode the escape sequences being exercised. The bare
// four-hex-digit universal-character-name form is assembled at runtime (a
// backslash string + the rest).
//

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "Parser.hpp"

namespace {
int failures = 0;
int checks = 0;

// Expects `converted` toothpicks after the rewrite and `encodings` backslashes of
// encoding escapes, which are excluded from the toothpick totals.
void Expect(const std::string_view label, const std::string_view body, const uint64_t converted,
            const uint64_t encodings = 0) {
  ++checks;
  Parser::RawLiteral literal;
  const uint64_t actual = Parser::CountRawStringToothpicks(body, &literal);
  if (actual == converted && literal.encodingBackslashes == encodings)
    return;
  ++failures;
  std::cerr << "string_literal_decode_test FAILED: " << label
            << " | body=<" << body << "> expected=" << converted << "/" << encodings
            << " actual=" << actual << "/" << literal.encodingBackslashes << '\n';
}

const std::string kBackslash = "\\"; // a single backslash, used to build universal-character-name inputs

// Expects the raw literal's content: decoded simple escapes, encoding escapes as written.
void ExpectContent(const std::string_view label, const std::string_view body, const std::string_view content) {
  ++checks;
  Parser::RawLiteral literal;
  Parser::CountRawStringToothpicks(body, &literal);
  if (literal.content == content)
    return;
  ++failures;
  std::cerr << "string_literal_decode_test FAILED: " << label
            << " | body=<" << body << "> content=<" << literal.content << "> expected=<" << content << ">\n";
}
} // namespace

int main() {
  // --- Escapes the rewrite removes ---
  Expect("plain text, no escapes", R"(plain text, no escapes here)", 0);
  Expect("empty body", R"()", 0);
  Expect("line feed and tab", R"(\t\n)", 0);
  Expect("quote, apostrophe, question mark", R"(\"\'\?)", 0);
  Expect("braces in plain text are harmless", R"(text { with } and { nested { braces } })", 0);

  // --- \\ leaves one backslash ---
  Expect("escaped backslash", R"(\\)", 1);
  Expect("two escaped backslashes", R"(\\\\)", 2);
  Expect("escaped backslash then literal n", R"(\\n)", 1);
  Expect("windows-style path", R"(C:\\dir\\sub\\file)", 3);
  Expect("backslash buried in plain text", R"(prefix \\ suffix)", 1);
  Expect("escaped backslash then literal '134' (not an octal escape)", R"(\\134)", 1);

  // --- Simple escapes for characters a raw literal cannot hold keep a toothpick ---
  Expect("control letters CR, VT, FF, BEL, BS", R"(\r\v\f\a\b)", 5);

  // --- Encoding escapes stay as written and are not counted ---
  Expect("hex escape", R"(\x41)", 0, 1);
  Expect("hex escape of a backslash", R"(\x5c)", 0, 1);
  Expect("hex escape eats all hex digits", R"(\x5cc)", 0, 1);
  Expect("two hex escapes of a UTF-8 character", R"(\xc3\xa9)", 0, 2);
  Expect("NUL as hex", R"(\x00)", 0, 1);
  Expect("octal escape", R"(\134)", 0, 1);
  Expect("octal limited to 3 digits: 01340 is one escape then '40'", R"(\01340)", 0, 1);
  Expect("octal 0 then literal '8'", R"(\08)", 0, 1);
  Expect("UCN backslash-u", kBackslash + "u0041", 0, 1);
  Expect("UCN backslash-U", R"(\U0000005c)", 0, 1);
  Expect("C++23 delimited hex", R"(\x{5c})", 0, 1);
  Expect("C++23 delimited octal", R"(\o{134})", 0, 1);
  Expect("C++23 delimited UCN", R"(\u{5c})", 0, 1);
  Expect("C++23 named UCN", R"(\N{REVERSE SOLIDUS})", 0, 1);
  Expect("C++23 named UCN, other name", R"(\N{LATIN SMALL LETTER A})", 0, 1);

  // --- Content of the rewrite ---
  ExpectContent("quote, tab, backslash, line feed", R"(q\"\t\\\n)", "q\"\t\\\n");
  ExpectContent("encoding escapes stay as written", R"(caf\xe9 \101)", R"(caf\xe9 \101)");
  ExpectContent("control letter decodes", R"(\r)", "\r");

  // --- Mixed ---
  Expect("escaped backslash, line feed, octal, hex", R"(a\\b\nc\134d\x5c)", 1, 2);
  Expect("one of each encoding form",
         R"(\134\x5c\U0000005c\x{5c}\o{134}\u{5c}\N{REVERSE SOLIDUS})", 0, 7);
  Expect("escapes mixed with braces", R"(\n {5c} \t)", 0);

  // --- Malformed inputs: best effort, must never crash ---
  Expect("dangling backslash (whole body is one backslash)", R"x(\)x", 0);
  Expect("text then dangling backslash", R"x(abc\)x", 0);
  Expect("backslash-x with no hex digits", R"(\xg)", 0);
  Expect("backslash-x at end of body", R"(\x)", 0);
  Expect("C++23 empty braces x{}", R"(\x{})", 0);
  Expect("UCN backslash-u with too few hex digits", kBackslash + "u00", 0);
  Expect("UCN backslash-U with 7 hex digits", R"(\U0000005)", 0);
  Expect("C++23 unterminated brace x{5c", R"(\x{5c)", 0);
  Expect("backslash-o without braces", R"(\o134)", 0);
  Expect("backslash-N without braces", R"(\Nfoo)", 0);

  // --- Documented over-collapse approximation (see Parser.hpp) ---
  // CountRawStringToothpicks always treats its input as the body of a *normal*
  // literal. If the same characters had been the body of a *raw* literal R"(\\)" —
  // true content two backslashes — the function still reports 1, not 2; and the
  // body of R"(\n)" — true content '\' 'n' — is reported as 0, not 1.
  Expect("raw-literal body two-backslashes over-collapses to 1", R"(\\)", 1);
  Expect("raw-literal body backslash-n over-collapses to 0", R"(\n)", 0);

  if (failures == 0) {
    std::cout << "string_literal_decode_test: all " << checks << " checks passed.\n";
    return 0;
  }
  std::cerr << "string_literal_decode_test: " << failures << " of " << checks << " checks failed.\n";
  return 1;
}
