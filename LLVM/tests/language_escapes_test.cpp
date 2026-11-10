//
// language_escapes_test.cpp
// Author: Antoine Bastide
//
// For every supported language, parses a small source file and checks each string
// sample the extractor emits: its body text (escapes included) and the toothpicks
// left after rewriting it as a raw literal (see RawRewrite.hpp). Expected counts
// follow each language's escape semantics.
//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "JSON.hpp"
#include "LanguageClassifier.hpp"
#include "LanguageParser.hpp"
#include "RawRewrite.hpp"

namespace fs = std::filesystem;

namespace {
  struct Sample {
    /// Expected body; nullopt skips the check where the body keeps its delimiters.
    std::optional<std::string_view> value;
    uint64_t converted;
    /// Expected rawHolds and mHolds flags; nullopt skips the check.
    std::optional<bool> raw = std::nullopt;
    std::optional<bool> m = std::nullopt;
  };

  struct Case {
    LanguageEnum language;
    std::string_view extension;
    std::string_view source;
    std::vector<Sample> strings;
    size_t comments = 0;
  };

  const std::vector<Case> &Cases() {
    static const std::vector<Case> cases = {
      {LanguageEnum::C, "c", R"src(
const char *a = "ab\\d\n";
const char *b = "\x00\x01";
const char *c = "\xc3\xa9";
const char *d = "\xe9\r";
const char *e = "\a\b\f\v\t\"";
)src", {{.value = R"(ab\\d\n)", .converted = 1, .raw = false, .m = true}, {.value = R"(\x00\x01)", .converted = 2, .raw = false, .m = true}, {.value = R"(\xc3\xa9)", .converted = 0}, {.value = R"(\xe9\r)", .converted = 1, .raw = false, .m = false}, {.value = R"(\a\b\f\v\t\")", .converted = 4}}},

      {LanguageEnum::CPP, "cpp", R"src(
const char *a = R"(r\n)";
const wchar_t *b = L"\xe9";
const char *c = u8"\u005C\N{REVERSE SOLIDUS}";
const char *d = "\0";
)src", {{.value = R"(r\n)", .converted = 1, .raw = true, .m = true}, {.value = R"(\xe9)", .converted = 0, .raw = true, .m = true}, {.value = R"(\u005C\N{REVERSE SOLIDUS})", .converted = 2}, {.value = R"(\0)", .converted = 1}}},

      {LanguageEnum::Go, "go", R"src(package p
var a = "ab\\d\n"
var b = `r\n`
var c = "\xc3\xa9"
var d = "\xe9"
var e = "\000\a\v"
var f = "\134\x5C\u005C\U0000005C"
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(r\n)", .converted = 1, .raw = true, .m = true}, {.value = R"(\xc3\xa9)", .converted = 0, .raw = true, .m = true}, {.value = R"(\xe9)", .converted = 1, .raw = false, .m = false}, {.value = R"(\000\a\v)", .converted = 3}, {.value = R"(\134\x5C\u005C\U0000005C)", .converted = 4}}},

      {LanguageEnum::Python, "py", R"src(
a = "ab\\d\n"
b = r"r\n"
c = b"\xe9"
d = "\xe9"
e = "\N{REVERSE SOLIDUS}\d"
f = "\N{NULL}\N{DEGREE SIGN}"
g = "\134\x5C\u005C\U0000005C\0"
)src", {{.value = R"(ab\\d\n)", .converted = 1, .raw = false, .m = true}, {.value = R"(r\n)", .converted = 1}, {.value = R"(\xe9)", .converted = 0}, {.value = R"(\xe9)", .converted = 0}, {.value = R"(\N{REVERSE SOLIDUS}\d)", .converted = 2, .raw = true, .m = true}, {.value = R"(\N{NULL}\N{DEGREE SIGN})", .converted = 1}, {.value = R"(\134\x5C\u005C\U0000005C\0)", .converted = 5}}},

      {LanguageEnum::JavaScript, "js", R"src(
a = "ab\\d\n";
b = `t\n${x}u`;
c = "\uD83D\uDE00";
d = "\uD83D";
e = "a\[b";
f = "\134\x5C\u{5C}\1";
g = "a`b(";
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(t\nu)", .converted = 0}, {.value = R"(\uD83D\uDE00)", .converted = 0}, {.value = R"(\uD83D)", .converted = 1, .raw = false, .m = false}, {.value = R"(a\[b)", .converted = 1, .raw = true, .m = false}, {.value = R"(\134\x5C\u{5C}\1)", .converted = 4}, {.value = R"(a`b()", .converted = 0, .raw = false, .m = false}}},

      {LanguageEnum::TypeScript, "ts", R"src(
let a: string = "ab\\d\n";
let b = "\x00";
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(\x00)", .converted = 1}}},

      {LanguageEnum::Java, "java", R"src(
class T {
  String a = "ab\\d\n";
  String b = "\0\s";
  String c = "\uD83D\uDE00";
  String d = "\134\177";
}
)src", {{.value = R"(ab\\d\n)", .converted = 1, .raw = false, .m = true}, {.value = R"(\0\s)", .converted = 1}, {.value = R"(\uD83D\uDE00)", .converted = 0}, {.value = R"(\134\177)", .converted = 2}}},

      {LanguageEnum::CSharp, "cs", R"src(
class T {
  string a = "ab\\d\n";
  string b = @"v\n";
  string c = "\e\x41";
  string d = "\x0000\u005C";
}
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(v\n)", .converted = 1}, {.value = R"(\e\x41)", .converted = 1}, {.value = R"(\x0000\u005C)", .converted = 2}}},

      {LanguageEnum::Rust, "rs", R"src(
fn f() {
  let a = "ab\\d\n";
  let b = r"r\n";
  let c = b"\xe9";
  let d = "\u{1F600}\0";
  let e = "\x00\x5C";
}
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(r\n)", .converted = 1}, {.value = R"(\xe9)", .converted = 0}, {.value = R"(\u{1F600}\0)", .converted = 1}, {.value = R"(\x00\x5C)", .converted = 2}}},

      {LanguageEnum::Ruby, "rb", R"src(
a = "ab\\d\n"
b = 'a\\b\n'
c = "\e\cA\u{41 42}"
d = "\xe9"
e = %q(a\\b\n(x))
f = "\134\x5C\0"
g = "\C-a\M-a\e"
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(a\\b\n)", .converted = 2, .raw = true, .m = true}, {.value = R"(\e\cA\u{41 42})", .converted = 2}, {.value = R"(\xe9)", .converted = 1}, {.value = R"(a\\b\n(x))", .converted = 2}, {.value = R"(\134\x5C\0)", .converted = 3}, {.value = R"(\C-a\M-a\e)", .converted = 3}}},

      {LanguageEnum::PHP, "php", R"src(<?php
$a = "ab\\d\n";
$b = 'a\\b\n';
$c = "\d\e";
$d = "\u{41}\x41";
$e = "\x00\101";
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(a\\b\n)", .converted = 2}, {.value = R"(\d\e)", .converted = 2}, {.value = R"(\u{41}\x41)", .converted = 0}, {.value = R"(\x00\101)", .converted = 1}}},

      {LanguageEnum::Perl, "pl", R"src(
my $a = "ab\\d\n";
my $b = 'a\\b\n';
my $c = "\e\cA\x{41}";
my $d = "\xe9";
my $e = "\x00\101\x{5C}";
my $f = q{a\\b\}};
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(a\\b\n)", .converted = 2}, {.value = R"(\e\cA\x{41})", .converted = 2}, {.value = R"(\xe9)", .converted = 1}, {.value = R"(\x00\101\x{5C})", .converted = 2}, {.value = std::nullopt, .converted = 3}}},

      {LanguageEnum::Lua, "lua", R"src(
a = "ab\\d\n"
b = [[l\n]]
c = "\092\z
     x"
d = "\xe9"
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(l\n)", .converted = 1}, {.value = std::nullopt, .converted = 1}, {.value = R"(\xe9)", .converted = 1}}},

      {LanguageEnum::Swift, "swift", R"src(
let a = "ab\\d\n"
let b = #"r\n"#
let c = #"a\#nb"#
let d = "\0\u{1F600}"
let e = "\u{5C}\u{7F}"
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = std::nullopt, .converted = 1}, {.value = std::nullopt, .converted = 0}, {.value = R"(\0\u{1F600})", .converted = 1}, {.value = R"(\u{5C}\u{7F})", .converted = 2}}},

      {LanguageEnum::Kotlin, "kt", R"src(
val a = "ab\\d\n"
val b = """r\n"""
val c = "\u005C\t"
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(r\n)", .converted = 1}, {.value = R"(\u005C\t)", .converted = 1}}},

      {LanguageEnum::R, "r", R"src(
a <- "ab\\d\n"
b <- r"(r\n)"
c <- "\xe9"
d <- "\x5C\134"
e <- "\u005C"
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = std::nullopt, .converted = 1}, {.value = R"(\xe9)", .converted = 1}, {.value = R"(\x5C\134)", .converted = 2}, {.value = R"(\u005C)", .converted = 1}}},

      {LanguageEnum::Scala, "scala", R"src(
object T {
  val a = "ab\\d\n"
  val b = raw"r\n"
  val c = """t\n"""
  val d = "\u0000\u005C"
}
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = std::nullopt, .converted = 1}, {.value = R"(t\n)", .converted = 1}, {.value = R"(\u0000\u005C)", .converted = 2}}},

      {LanguageEnum::Haskell, "hs", R"src(
a = "ab\\d\n"
b = "\SOH\&x\92\
    \y"
c = '\n'
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = std::nullopt, .converted = 2}, {.value = R"(\n)", .converted = 0}}},

      {LanguageEnum::OCaml, "ml", R"src(
let a = "ab\\d\n"
let b = {|q\n|}
let c = "\092\o134\d"
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(q\n)", .converted = 1}, {.value = R"(\092\o134\d)", .converted = 3}}},

      {LanguageEnum::Erlang, "erl", R"src(
f() -> {"ab\\d\n", "\^A\d\x{41}", ~S"s\n", ~s"t\n", $\n}.
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(\^A\d\x{41})", .converted = 2}, {.value = std::nullopt, .converted = 1}, {.value = std::nullopt, .converted = 0}, {.value = std::nullopt, .converted = 0}}},

      {LanguageEnum::Elixir, "ex", R"src(
a = "ab\\d\n"
b = ~S(s\n)
c = "\e\xe9"
d = "\d\x00\x41"
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(s\n)", .converted = 1}, {.value = R"(\e\xe9)", .converted = 2}, {.value = R"(\d\x00\x41)", .converted = 2}}},

      {LanguageEnum::Dart, "dart", R"src(
var a = "ab\\d\n";
var b = r"r\n";
var c = "\x00\0";
var d = "\x5C\u{5C}\u005C";
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(r\n)", .converted = 1}, {.value = R"(\x00\0)", .converted = 1}, {.value = R"(\x5C\u{5C}\u005C)", .converted = 3}}},

      {LanguageEnum::Objective_C, "m", R"src(
const char *a = "ab\\d\n";
NSString *b = @"\xc3\xa9\a";
const char *c = "\x00\134";
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(\xc3\xa9\a)", .converted = 1}, {.value = R"(\x00\134)", .converted = 2}}},

      {LanguageEnum::GLSL, "glsl", R"src(#version 450
// comment with an unmatched (
void main() {}
)src", {}, 1},

      {LanguageEnum::HLSL, "hlsl", R"src(
string s = "ab\\d\n";
string t = "\x00\134";
)src", {{.value = R"(ab\\d\n)", .converted = 1}, {.value = R"(\x00\134)", .converted = 2}}},
    };
    return cases;
  }

  struct RawCase {
    LanguageEnum language;
    std::string content;
    std::string_view quote;
    bool holds;
  };

  // DefaultRawHolds: one holding and one failing content per language, plus the
  // edge each rule names.
  const std::vector<RawCase> &RawCases() {
    const std::string bs = "\\";
    static const std::vector<RawCase> cases = {
      {LanguageEnum::CPP, "a)\"b", "", false}, {LanguageEnum::CPP, "a\"b)", "", true},
      {LanguageEnum::Go, "a`b", "", false}, {LanguageEnum::Go, "a\rb", "", false}, {LanguageEnum::Go, "a" + bs + "\"", "", true},
      {LanguageEnum::Python, "a\"b", "\"", false}, {LanguageEnum::Python, "a\"b", "'", true},
      {LanguageEnum::Python, "a" + bs, "\"", false}, {LanguageEnum::Python, "a" + bs + "\"b", "\"", true},
      {LanguageEnum::Python, "a\nb", "\"", false}, {LanguageEnum::Python, "a\nb", "\"\"\"", true},
      {LanguageEnum::Python, "a\"", "\"\"\"", false},
      {LanguageEnum::JavaScript, "a`b", "", false}, {LanguageEnum::JavaScript, "a${b}", "", false},
      {LanguageEnum::JavaScript, "a$b{", "", true}, {LanguageEnum::JavaScript, "a" + bs, "", false},
      {LanguageEnum::TypeScript, "a" + bs + "`", "", true},
      {LanguageEnum::CSharp, "a\"b", "", false}, {LanguageEnum::CSharp, "a" + bs + "b", "", true},
      {LanguageEnum::Rust, "a\"b", "", false}, {LanguageEnum::Rust, "a" + bs + "b", "", true},
      {LanguageEnum::Ruby, "a'b", "", false}, {LanguageEnum::Ruby, "a" + bs + "b", "", true},
      {LanguageEnum::PHP, "a" + bs + bs + "b", "", false}, {LanguageEnum::Perl, "a" + bs, "", false},
      {LanguageEnum::Lua, "a]]b", "", false}, {LanguageEnum::Lua, "\na", "", false}, {LanguageEnum::Lua, "a]b", "", true},
      {LanguageEnum::Swift, "a\"#b", "", false}, {LanguageEnum::Swift, "a" + bs + "#n", "", false},
      {LanguageEnum::Swift, "a\nb", "", false}, {LanguageEnum::Swift, "a\"b", "", true},
      {LanguageEnum::Kotlin, "a\"\"\"b", "", false}, {LanguageEnum::Kotlin, "a$b", "", false},
      {LanguageEnum::Kotlin, "a${b}", "", false}, {LanguageEnum::Kotlin, "a$1\"", "", true},
      {LanguageEnum::R, "a)\"b", "", false}, {LanguageEnum::R, "a\"b", "", true},
      {LanguageEnum::Scala, "a\"\"\"b", "", false}, {LanguageEnum::Scala, bs + "u0041", "", false},
      {LanguageEnum::Scala, "a" + bs + "nb\"", "", true},
      {LanguageEnum::OCaml, "a|}b", "", false}, {LanguageEnum::OCaml, "a|b", "", true},
      {LanguageEnum::Erlang, "a\"b", "", false}, {LanguageEnum::Erlang, "a" + bs + "b", "", true},
      {LanguageEnum::Elixir, "a\"b", "", false}, {LanguageEnum::Elixir, "a" + bs, "", false},
      {LanguageEnum::Elixir, "a" + bs + "b", "", true},
      {LanguageEnum::Dart, "a\"b", "\"", false}, {LanguageEnum::Dart, "a\"b", "'", true},
      {LanguageEnum::Dart, "a\nb", "'", false}, {LanguageEnum::Dart, "a'b\n", "'''", true},
      {LanguageEnum::C, "abc", "", false}, {LanguageEnum::Java, "abc", "", false},
      {LanguageEnum::Haskell, "abc", "", false}, {LanguageEnum::Objective_C, "abc", "", false},
      {LanguageEnum::GLSL, "abc", "", false}, {LanguageEnum::HLSL, "abc", "", false},
    };
    return cases;
  }

  size_t RunRawCases() {
    size_t failures = 0;
    for (const RawCase &c: RawCases())
      if (RawRewrite::DefaultRawHolds(c.language, c.content, c.quote) != c.holds) {
        std::cerr << LanguageName(c.language) << ": raw form holds <" << c.content << "> quote <" << c.quote
                  << ">, expected " << c.holds << '\n';
        ++failures;
      }
    return failures;
  }

  bool Run(const Case &c, const fs::path &dir) {
    const fs::path path = dir / ("sample." + std::string(c.extension));
    std::ofstream(path, std::ios::binary) << c.source;

    const std::string name(LanguageName(c.language));
    Serde::JSON result;
    if (!LanguageParser::ExtractData(c.language, path.string(), result)) {
      std::cerr << name << ": extraction failed\n";
      return false;
    }

    bool ok = true;
    size_t strings = 0, comments = 0;
    for (const auto &e: result.GetArray()) {
      if (e["kind"].GetString() != "string") {
        ++comments;
        continue;
      }
      const std::string value = e["value"].GetString();
      const auto converted = static_cast<uint64_t>(e["convertedToothpicks"].GetNumber());
      const bool raw = e["rawHolds"].GetNumber() != 0.0;
      const bool m = e["mHolds"].GetNumber() != 0.0;
      if (strings >= c.strings.size()) {
        std::cerr << name << ": unexpected string <" << value << ">\n";
        ok = false;
      } else if (const Sample &want = c.strings[strings];
                 (want.value && value != *want.value) || converted != want.converted ||
                 (want.raw && raw != *want.raw) || (want.m && m != *want.m)) {
        std::cerr << name << ": string " << strings << " <" << value << "> converted=" << converted << " raw=" << raw << " m=" << m
                  << ", expected <" << want.value.value_or("(any)") << "> converted=" << want.converted
                  << " raw=" << (want.raw ? std::to_string(*want.raw) : "(any)")
                  << " m=" << (want.m ? std::to_string(*want.m) : "(any)") << '\n';
        ok = false;
      }
      ++strings;
    }
    if (strings != c.strings.size() || comments != c.comments) {
      std::cerr << name << ": " << strings << " strings and " << comments << " comments, expected "
                << c.strings.size() << " and " << c.comments << '\n';
      ok = false;
    }
    return ok;
  }
}

// A file the tree-sitter path does not parse reports why, instead of passing
// as a parsed file with no samples.
size_t RunSkipCases(const fs::path &dir) {
  const fs::path path = dir / "binary.go";
  std::ofstream(path, std::ios::binary) << std::string("package p\nvar s = \"x\"\n\0", 23);
  Serde::JSON result;
  SkipReason skip = SkipReason::None;
  if (LanguageParser::ExtractData(LanguageEnum::Go, path.string(), result, &skip) ||
      skip != SkipReason::Binary) {
    std::cerr << "skip: a Go file containing NUL was not reported as skipped (binary)\n";
    return 1;
  }
  return 0;
}

int main() {
  const fs::path dir = fs::temp_directory_path() / "language_escapes_test";
  fs::create_directories(dir);

  size_t failures = RunRawCases() + RunSkipCases(dir);
  for (const Case &c: Cases())
    failures += Run(c, dir) ? 0 : 1;

  if (failures == 0) {
    std::cout << "language_escapes_test: all " << Cases().size() << " languages and " << RawCases().size()
              << " raw-form cases passed.\n";
    return 0;
  }
  std::cerr << "language_escapes_test: " << failures << " checks failed.\n";
  return 1;
}
