//
// string_literal_decode_test.cpp
// Author: Antoine Bastide
// Date: 12.05.2026
//
// Unit tests for Parser::CountRawStringToothpicks — given the *body* of a C/C++
// string literal (the text between the quotes, line splices already removed), it
// returns how many backslash bytes the literal's decoded content holds, i.e. how
// many toothpicks survive a rewrite to a raw string literal.
//
// Test inputs are written as raw string literals R"(...)" so the test's own
// compiler does not pre-decode the escape sequences being exercised. The bare
// four-hex-digit "\uXXXX" universal-character-name form is assembled at runtime
// (a backslash string + the rest), because writing it literally here is risky.
//

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "Parser.hpp"

namespace {
int failures = 0;
int checks = 0;

void Expect(const std::string_view label, const std::string_view body, const uint64_t expected) {
  ++checks;
  const uint64_t actual = Parser::CountRawStringToothpicks(body);
  if (actual == expected)
    return;
  ++failures;
  std::cerr << "string_literal_decode_test FAILED: " << label
            << " | body=<" << body << "> expected=" << expected
            << " actual=" << actual << '\n';
}

const std::string kBackslash = "\\"; // a single backslash, used to build "\uXXXX" inputs
} // namespace

int main() {
  // --- Happy path: a single escape sequence that decodes to one backslash (0x5C) ---
  Expect("escaped backslash", R"(\\)", 1);
  Expect("octal escape 134", R"(\134)", 1);
  Expect("hex escape x5c (lower)", R"(\x5c)", 1);
  Expect("hex escape x5C (upper digit)", R"(\x5C)", 1);
  Expect("hex escape x05c (leading zero, == 0x5c)", R"(\x05c)", 1);
  Expect("UCN backslash-u 005c (lower u)", kBackslash + "u005c", 1);
  Expect("UCN backslash-u 005C (lower u, upper hex)", kBackslash + "u005C", 1);
  Expect("UCN backslash-U 0000005c (upper U)", R"(\U0000005c)", 1);
  Expect("C++23 delimited hex x{5c}", R"(\x{5c})", 1);
  Expect("C++23 delimited hex x{5C}", R"(\x{5C})", 1);
  Expect("C++23 delimited hex x{00005c}", R"(\x{00005c})", 1);
  Expect("C++23 delimited octal o{134}", R"(\o{134})", 1);
  Expect("C++23 delimited UCN u{5c}", R"(\u{5c})", 1);
  Expect("C++23 delimited UCN u{0000005c}", R"(\u{0000005c})", 1);
  Expect("C++23 named UCN N{REVERSE SOLIDUS}", R"(\N{REVERSE SOLIDUS})", 1);

  // --- Happy path: counting multiple / mixed sequences ---
  Expect("two escaped backslashes", R"(\\\\)", 2);
  Expect("escaped backslash then literal n", R"(\\n)", 1);                  // decoded: '\' 'n'
  Expect("windows-style path", R"(C:\\dir\\sub\\file)", 3);
  Expect("mix of escaped backslash, n, octal, hex", R"(a\\b\nc\134d\x5c)", 3); // \\, \134, \x5c => 3 ; \n => 0
  Expect("one of each non-bare backslash-producing escape form",
         R"(\\\134\x5c\U0000005c\x{5c}\o{134}\u{5c}\N{REVERSE SOLIDUS})", 8);
  Expect("backslash buried in plain text", R"(prefix \\ suffix)", 1);
  Expect("three octal backslashes", R"(\134\134\134)", 3);

  // --- Sad path: escapes that do NOT decode to a backslash ---
  Expect("plain text, no escapes", R"(plain text, no escapes here)", 0);
  Expect("empty body", R"()", 0);
  Expect("simple escapes only", R"(\n\t\r\v\f\a\b)", 0);
  Expect("escaped quote / apostrophe / question mark", R"(\n\t\"\'\?)", 0);
  Expect("hex escape x41 ('A')", R"(\x41)", 0);
  Expect("hex x15c is 0x15C, not 0x5C (no low-byte truncation)", R"(\x15c)", 0);
  Expect("greedy hex eats trailing hex: x5cc is 0x5CC", R"(\x5cc)", 0);
  Expect("greedy hex eats trailing hex: x5ce is 0x5CE", R"(\x5ce)", 0);
  Expect("octal 534 is 0o534 == 348, not 0x5C", R"(\534)", 0);
  Expect("octal 0 then literal 'x5c'", R"(\0x5c)", 0);
  Expect("octal 0 then literal '8' (8 is not an octal digit)", R"(\08)", 0);
  Expect("octal limited to 3 digits: 01340 is 0o013 then '40'", R"(\01340)", 0);
  Expect("UCN backslash-u 0041 ('A')", kBackslash + "u0041", 0);
  Expect("UCN backslash-U 00000041 ('A')", R"(\U00000041)", 0);
  Expect("C++23 braced hex x{41} ('A')", R"(\x{41})", 0);
  Expect("C++23 braced octal o{101} (== 65, 'A')", R"(\o{101})", 0);
  Expect("C++23 named UCN, different name", R"(\N{LATIN SMALL LETTER A})", 0);

  // --- Weird / malformed inputs: best effort, must never over-count or crash ---
  Expect("dangling backslash (whole body is one backslash)", R"x(\)x", 0);
  Expect("text then dangling backslash", R"x(abc\)x", 0);
  Expect("escaped backslash then literal '134' (not an octal escape)", R"(\\134)", 1);
  Expect("escaped backslash then literal 'x5c' (not a hex escape)", R"(\\x5c)", 1);
  Expect("backslash-x with no hex digits", R"(\xg)", 0);
  Expect("backslash-x at end of body", R"(\x)", 0);
  Expect("UCN backslash-u with too few hex digits", kBackslash + "u00", 0);
  Expect("UCN backslash-u 005 (only 3 hex digits)", kBackslash + "u005", 0);
  Expect("UCN backslash-U 0000005 (only 7 hex digits)", R"(\U0000005)", 0);
  Expect("C++23 empty braces x{}", R"(\x{})", 0);
  Expect("C++23 unterminated brace x{5c", R"(\x{5c)", 0);
  Expect("C++23 unterminated brace o{134", R"(\o{134)", 0);
  Expect("backslash-o without braces is not an escape", R"(\o134)", 0);
  Expect("backslash-N without braces", R"(\Nfoo)", 0);
  Expect("backslash-N{ name with surrounding spaces }", R"(\N{ REVERSE SOLIDUS })", 0);
  Expect("backslash-N{REVERSE SOLIDUSX} (no exact closing brace)", R"(\N{REVERSE SOLIDUSX})", 0);
  Expect("braces in plain text are harmless", R"(text { with } and { nested { braces } })", 0);
  Expect("non-escape sequences mixed with braces", R"(\n {5c} \t)", 0);

  // --- Documented over-collapse approximation (see Parser.hpp) ---
  // CountRawStringToothpicks always decodes as if its input were a *normal* literal
  // body. If the same characters had been the body of a *raw* literal R"(\\)" — true
  // content two backslashes — the function still reports 1, not 2; and the body of
  // R"(\n)" — true content '\' 'n' — is reported as 0, not 1.
  Expect("raw-literal body two-backslashes over-collapses to 1", R"(\\)", 1);
  Expect("raw-literal body backslash-n over-collapses to 0", R"(\n)", 0);

  if (failures == 0) {
    std::cout << "string_literal_decode_test: all " << checks << " checks passed.\n";
    return 0;
  }
  std::cerr << "string_literal_decode_test: " << failures << " of " << checks << " checks failed.\n";
  return 1;
}
