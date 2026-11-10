//
// string_literal_decode_test.cpp
// Author: Antoine Bastide
// Date: 12.05.2026
//
// Unit tests for Parser::CountRawStringToothpicks — given the *body* of a C/C++
// string literal (the text between the quotes, line splices already removed), it
// returns how many toothpicks survive a rewrite to a raw string literal. Every
// escape is decoded and loses its backslash, encoding escapes (\x41, \101,
// \u0041, \N{...}) included; \\ and any escape that decodes to a backslash leave
// one. Tab, line feed, and carriage return are written as themselves; other
// control characters go into the content verbatim but keep their toothpick, as
// does a byte that is not valid UTF-8.
//
// Test inputs are written as raw string literals R"(...)" so the test's own
// compiler does not pre-decode the escape sequences being exercised. The bare
// four-hex-digit universal-character-name form is assembled at runtime (a
// backslash string + the rest).
//

#include <bit>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "LanguageClassifier.hpp"
#include "Parser.hpp"
#include "RawRewrite.hpp"

using namespace std::string_view_literals;

namespace {
int failures = 0;
int checks = 0;

// Expects `converted` toothpicks after the rewrite of a narrow literal, or of a
// wide one (L, u, U) if `wide` is set.
void Expect(const std::string_view label, const std::string_view body, const uint64_t converted,
            const bool wide = false) {
  ++checks;
  Parser::RawLiteral literal;
  const uint64_t actual = Parser::CountRawStringToothpicks(body, &literal, wide);
  if (actual == converted)
    return;
  ++failures;
  std::cerr << "string_literal_decode_test FAILED: " << label
            << " | body=<" << body << "> expected=" << converted << " actual=" << actual << '\n';
}

const std::string kBackslash = "\\"; // a single backslash, used to build universal-character-name inputs

// Expects the raw literal's content: every escape decoded and normalized.
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

  // --- Tab, LF and CR lose their backslash; other control characters keep it ---
  Expect("CR loses its toothpick; VT, FF, BEL, BS keep theirs", R"(\r\v\f\a\b)", 4);
  Expect("CR LF", R"(\r\n)", 0);

  // --- Encoding escapes are decoded ---
  Expect("hex escape", R"(\x41)", 0);
  Expect("hex escape of a backslash", R"(\x5c)", 1);
  Expect("hex escape eats all hex digits: x5cc is U+05CC", R"(\x5cc)", 0);
  Expect("two hex escapes of a UTF-8 character", R"(\xc3\xa9)", 0);
  Expect("lone byte that is not UTF-8 keeps its toothpick", R"(\xe9)", 1);
  Expect("wide literal: xe9 is U+00E9", R"(\xe9)", 0, true);
  Expect("NUL as hex keeps its toothpick", R"(\x00)", 1);
  Expect("octal escape of a backslash", R"(\134)", 1);
  Expect("octal limited to 3 digits: 01340 is VT then '40'", R"(\01340)", 1);
  Expect("octal 0 (NUL) then literal '8'", R"(\08)", 1);
  Expect("UCN backslash-u", kBackslash + "u0041", 0);
  Expect("UCN backslash-U of a backslash", R"(\U0000005c)", 1);
  Expect("UCN of a lone surrogate keeps its toothpick", kBackslash + "uD800", 1);
  Expect("C++23 delimited hex", R"(\x{5c})", 1);
  Expect("C++23 delimited octal", R"(\o{134})", 1);
  Expect("C++23 delimited UCN", R"(\u{5c})", 1);
  Expect("C++23 named UCN", R"(\N{REVERSE SOLIDUS})", 1);
  Expect("C++23 named UCN, other name", R"(\N{LATIN SMALL LETTER A})", 0);

  // --- Content of the rewrite ---
  ExpectContent("quote, tab, backslash, line feed", R"(q\"\t\\\n)", "q\"\t\\\n");
  ExpectContent("encoding escapes decode", R"(caf\xc3\xa9 \101)", "café A");
  ExpectContent("encoded matcher decodes", R"(\x5B\135)", "[]");
  ExpectContent("CR LF and lone CR stay as written", R"(a\r\nb\rc)", "a\r\nb\rc");
  ExpectContent("encoded CR LF stays CR LF", R"(\x0d\x0a)", "\r\n");
  ExpectContent("FF and VT stay as themselves", R"(a\fb\vc)", "a\fb\vc");
  ExpectContent("other control characters are kept verbatim", R"(a\a\b\x00\x1b\177b)", "a\a\b\0\x1b\x7f" "b"sv);

  // --- Mixed ---
  Expect("escaped backslash, line feed, octal, hex", R"(a\\b\nc\134d\x5c)", 3);
  Expect("one of each encoding form",
         R"(\134\x5c\U0000005c\x{5c}\o{134}\u{5c}\N{REVERSE SOLIDUS})", 7);
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

  // --- Native raw forms ---
  // The first form of each language is the default form of DefaultRawHolds.
  constexpr std::string_view kContents[] = {
    "plain", R"(say "hi")", R"(C:\dir\)", "it's", "line\nbreak", "cr\rhere", R"x(f(x)")x", "a`b",
    R"(x"#y)", "]]", "\nlead", "${x}", "$name", R"(\u0041)", R"(""")", "|}", R"(\#)", R"(a\)",
  };
  for (size_t l = 0; l < static_cast<size_t>(LanguageEnum::COUNT); ++l) {
    const auto language = static_cast<LanguageEnum>(l);
    if (RawRewrite::RawFormChecks(language).empty())
      continue;
    for (const std::string_view content: kContents) {
      ++checks;
      const uint64_t holding = RawRewrite::RawFormsHolding(
        language, RawRewrite::RawFormViolations(language, content), true);
      if (static_cast<bool>(holding & 1) == RawRewrite::DefaultRawHolds(language, content, {}))
        continue;
      ++failures;
      std::cerr << "string_literal_decode_test FAILED: default raw form of " << LanguageName(language)
                << " disagrees with DefaultRawHolds on <" << content << ">\n";
    }
  }
  const auto expectForms = [&](const std::string_view label, const LanguageEnum language,
                               const std::string_view content, const uint64_t holding) {
    ++checks;
    const uint64_t actual = RawRewrite::RawFormsHolding(
      language, RawRewrite::RawFormViolations(language, content), true);
    if (actual == holding)
      return;
    ++failures;
    std::cerr << "string_literal_decode_test FAILED: " << label << " forms=" << actual << " expected=" << holding << '\n';
  };
  expectForms("Python: no raw form ends in a lone backslash", LanguageEnum::Python, R"(C:\dir\)", 0);
  expectForms("Python: a quote rules out its own forms only", LanguageEnum::Python, R"(say "hi")", 0b1010);
  expectForms("Rust: x\"#y needs two hashes", LanguageEnum::Rust, R"(x"#y)", 0b1100);
  expectForms("Rust: no hash level holds a CR", LanguageEnum::Rust, "cr\r", 0);
  expectForms("C++: )\" ends R\"(...)\"", LanguageEnum::CPP, R"x(f(x)")x", 0);
  expectForms("Go: a backtick ends `...`", LanguageEnum::Go, "a`b", 0);
  expectForms("Go: control characters are held verbatim", LanguageEnum::Go, "bell\a", 1);
  ++checks;
  if (RawRewrite::RawFormsHolding(LanguageEnum::Go, 0, false) != 0) {
    ++failures;
    std::cerr << "string_literal_decode_test FAILED: no form holds content that is not encodable\n";
  }

  // --- Raw forms of every language ---
  {
    size_t allChecks = 0, allForms = 0;
    for (const auto &family: RawRewrite::RawFormFamilies()) {
      allChecks += family.checks.size();
      for (size_t i = 0; i < family.checks.size(); ++allForms)
        for (const std::string_view form = family.checks[i].form; i < family.checks.size() && family.checks[i].form == form;)
          ++i;
    }
    ++checks;
    if (allChecks > 64 || allForms > 64) {
      ++failures;
      std::cerr << "string_literal_decode_test FAILED: " << allChecks << " checks and " << allForms
                << " forms do not fit 64-bit masks\n";
    }
  }
  // A language's own forms in the all-language masks hold exactly what its own forms hold.
  for (size_t l = 0; l < static_cast<size_t>(LanguageEnum::COUNT); ++l) {
    const auto language = static_cast<LanguageEnum>(l);
    const uint64_t own = RawRewrite::OwnRawForms(language);
    for (const std::string_view content: kContents) {
      ++checks;
      const uint64_t all = RawRewrite::AllRawFormsHolding(RawRewrite::AllRawFormViolations(content), true);
      const uint64_t mine = RawRewrite::RawFormsHolding(language, RawRewrite::RawFormViolations(language, content), true);
      if (std::popcount(all & own) == std::popcount(mine) && ((all & own) != 0) == (mine != 0))
        continue;
      ++failures;
      std::cerr << "string_literal_decode_test FAILED: own raw forms of " << LanguageName(language)
                << " disagree with the all-language masks on <" << content << ">\n";
    }
  }
  ++checks;
  if (RawRewrite::OwnRawForms(LanguageEnum::C) != 0 || RawRewrite::OwnRawForms(LanguageEnum::JavaScript) !=
      RawRewrite::OwnRawForms(LanguageEnum::TypeScript)) {
    ++failures;
    std::cerr << "string_literal_decode_test FAILED: C has no own raw form; JavaScript and TypeScript share one\n";
  }
  ++checks;
  if (RawRewrite::AllRawFormsHolding(RawRewrite::AllRawFormViolations(R"(C:\dir\)"), true) == 0) {
    ++failures;
    std::cerr << "string_literal_decode_test FAILED: some language's raw form holds C:\\dir\\\n";
  }

  // --- Control characters other than tab, LF and CR ---
  const auto expectControl = [&](const std::string_view label, const std::string_view content, const bool control) {
    ++checks;
    if (RawRewrite::HasControl(content) == control)
      return;
    ++failures;
    std::cerr << "string_literal_decode_test FAILED: " << label << '\n';
  };
  expectControl("tab, LF and CR are not control characters here", "a\tb\r\nc", false);
  expectControl("BEL is a control character", "a\ab", true);
  expectControl("NUL is a control character", "a\0b"sv, true);
  expectControl("U+0085 (NEL) is a C1 control character", "a\xc2\x85", true);
  expectControl("a lone byte above 0x7F is not a control character", "a\x85", false);

  if (failures == 0) {
    std::cout << "string_literal_decode_test: all " << checks << " checks passed.\n";
    return 0;
  }
  std::cerr << "string_literal_decode_test: " << failures << " of " << checks << " checks failed.\n";
  return 1;
}
