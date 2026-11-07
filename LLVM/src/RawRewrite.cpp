//
// RawRewrite.cpp
// Author: Antoine Bastide
//

#include "../include/RawRewrite.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

#include "../include/MatcherText.hpp"

namespace {
  constexpr uint32_t kBackslash = 0x5C;
  /// Above the Unicode range; numeric values saturate here.
  constexpr uint32_t kInvalid = 0x110000;

  int digitValue(const char c, const uint32_t base) {
    int d = -1;
    if (c >= '0' && c <= '9')
      d = c - '0';
    else if (c >= 'a' && c <= 'f')
      d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      d = c - 'A' + 10;
    return d >= 0 && static_cast<uint32_t>(d) < base ? d : -1;
  }

  /// Parses the digits of `base` in `text` from `i`; returns how many were read.
  size_t parseDigits(const std::string_view text, size_t i, const uint32_t base, uint32_t &value) {
    value = 0;
    const size_t begin = i;
    for (int d; i < text.size() && (d = digitValue(text[i], base)) >= 0; ++i)
      value = std::min(value * base + static_cast<uint32_t>(d), kInvalid);
    return i - begin;
  }

  /// True if `text` from `i` is exactly "{digits}" in `base`.
  bool parseBraced(const std::string_view text, const size_t i, const uint32_t base, uint32_t &value) {
    return i + 2 < text.size() && text[i] == '{' && text.back() == '}' &&
           parseDigits(text, i + 1, base, value) == text.size() - i - 2;
  }

  /// Characters that a raw literal cannot hold literally.
  bool nonHoldable(const uint32_t cp) {
    return (cp < 0x20 && cp != '\t' && cp != '\n') || (cp >= 0x7F && cp <= 0x9F);
  }

  uint32_t controlCode(const char c) {
    switch (c) {
      case 'a': return 0x07;
      case 'b': return 0x08;
      case 'd': return 0x7F;
      case 'e': return 0x1B;
      case 'f': return 0x0C;
      case 'n': return 0x0A;
      case 'r': return 0x0D;
      case 't': return 0x09;
      default: return 0x0B; // 'v'
    }
  }

  /// Haskell ASCII control names.
  constexpr std::array<std::string_view, 34> kAsciiNames = {
    "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS", "HT", "LF", "VT",
    "FF", "CR", "SO", "SI", "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
    "CAN", "EM", "SUB", "ESC", "FS", "GS", "RS", "US", "SP", "DEL"
  };

  bool asciiName(const std::string_view name) {
    return std::ranges::find(kAsciiNames, name) != kAsciiNames.end();
  }

  /// Decodes one strict UTF-8 sequence at `i`; returns its length, or 0 if invalid.
  size_t decodeUtf8(const std::string &bytes, const size_t i, uint32_t &cp) {
    const auto b = static_cast<uint8_t>(bytes[i]);
    size_t len;
    uint8_t lo = 0x80, hi = 0xBF;
    if (b < 0x80) {
      cp = b;
      return 1;
    }
    if (b >= 0xC2 && b <= 0xDF) {
      len = 2;
      cp = b & 0x1F;
    } else if (b >= 0xE0 && b <= 0xEF) {
      len = 3;
      cp = b & 0x0F;
      lo = b == 0xE0 ? 0xA0 : 0x80;
      hi = b == 0xED ? 0x9F : 0xBF;
    } else if (b >= 0xF0 && b <= 0xF4) {
      len = 4;
      cp = b & 0x07;
      lo = b == 0xF0 ? 0x90 : 0x80;
      hi = b == 0xF4 ? 0x8F : 0xBF;
    } else
      return 0;
    if (i + len > bytes.size())
      return 0;
    for (size_t k = 1; k < len; ++k) {
      const auto c = static_cast<uint8_t>(bytes[i + k]);
      if (c < (k == 1 ? lo : 0x80) || c > (k == 1 ? hi : 0xBF))
        return 0;
      cp = cp << 6 | (c & 0x3F);
    }
    return len;
  }
}

namespace RawRewrite {
  void Counter::Reset(const Dialect &next) {
    dialect = next;
    bytes.clear();
    owner.clear();
    survives.clear();
    encodingBackslashes = 0;
  }

  void Counter::push(const uint8_t byte, const int32_t id) {
    bytes.push_back(static_cast<char>(byte));
    owner.push_back(id);
  }

  void Counter::Literal(const std::string_view text) {
    for (const char c: text)
      push(static_cast<uint8_t>(c), kLiteral);
  }

  /// An escape that encodes a character by number or name stays as written in the
  /// raw literal, and its backslashes are not toothpicks.
  void Counter::encoding(const std::string_view text) {
    for (const char c: text)
      push(static_cast<uint8_t>(c), kEncoding);
    encodingBackslashes += static_cast<uint64_t>(std::ranges::count(text, '\\'));
  }

  bool Counter::isEncoding(const std::string_view rest) const {
    uint32_t value = 0;
    if (rest.size() >= 2 && asciiName(rest))
      return true; // Haskell \NUL, \SOH, ...
    const char c = rest[0];
    switch (c) {
      case 'x':
        return parseBraced(rest, 1, 16, value) || (rest.size() > 1 && parseDigits(rest, 1, 16, value) == rest.size() - 1);
      case 'o':
        return parseBraced(rest, 1, 8, value) || (rest.size() > 1 && parseDigits(rest, 1, 8, value) == rest.size() - 1);
      case 'u': {
        if (rest.size() == 5 && parseDigits(rest, 1, 16, value) == 4)
          return true;
        // \u{...}, with several space-separated code points in Ruby.
        if (rest.size() < 4 || rest[1] != '{' || rest.back() != '}')
          return false;
        const std::string_view list = rest.substr(2, rest.size() - 3);
        return list.find_first_not_of("0123456789abcdefABCDEF ") == std::string_view::npos &&
               list.find_first_not_of(' ') != std::string_view::npos;
      }
      case 'U':
        return rest.size() == 9 && parseDigits(rest, 1, 16, value) == 8;
      case 'N':
        return rest.size() > 3 && rest[1] == '{' && rest.back() == '}';
      case '^':
        return rest.size() == 2; // Haskell and Erlang control characters
      default:
        if (c >= '0' && c <= '9' && dialect.digits != Dialect::Digits::None)
          return parseDigits(rest, 0, dialect.digits == Dialect::Digits::Octal ? 8 : 10, value) == rest.size();
        // Ruby and Perl \cX, \C-x, \M-x. The Ruby grammar marks only \c, \C, or \M
        // and leaves the rest as content.
        return dialect.controlLetters && (c == 'c' || ((c == 'C' || c == 'M') && (rest.size() == 1 || rest[1] == '-')));
    }
  }

  void Counter::Escape(const std::string_view text) {
    const auto id = static_cast<int32_t>(survives.size());
    survives.push_back(0);

    const std::string_view rest = text.substr(1);
    // A dangling backslash, a line continuation, or a Haskell string gap.
    if (rest.empty() || rest[0] == ' ' || rest[0] == '\t' || rest[0] == '\n' || rest[0] == '\r' ||
        dialect.empty.find(rest[0]) != std::string_view::npos)
      return;
    if (isEncoding(rest))
      return encoding(text);

    const char c = rest[0];
    if (dialect.controls.find(c) != std::string_view::npos)
      return push(static_cast<uint8_t>(controlCode(c)), id);
    // Identity escape: decodes to the escaped character itself.
    if (dialect.unknownKeepsBackslash && c != '\\' && c != '"' && c != '\'' && c != '$')
      return Literal(text);
    for (const char r: rest)
      push(static_cast<uint8_t>(r), id);
  }

  uint64_t Counter::Count() {
    uint64_t literalBackslashes = 0;
    for (size_t i = 0; i < bytes.size();) {
      uint32_t cp = 0;
      const size_t len = decodeUtf8(bytes, i, cp);
      if (len == 0) {
        if (owner[i] >= 0)
          survives[static_cast<size_t>(owner[i])] = 1;
        ++i;
        continue;
      }
      if (cp == kBackslash) {
        if (owner[i] == kLiteral)
          ++literalBackslashes;
        else if (owner[i] >= 0 && survives[static_cast<size_t>(owner[i])] == 0)
          survives[static_cast<size_t>(owner[i])] = 2;
      } else if (nonHoldable(cp)) {
        for (size_t k = i; k < i + len; ++k)
          if (owner[k] >= 0)
            survives[static_cast<size_t>(owner[k])] = 1;
      }
      i += len;
    }
    return literalBackslashes + static_cast<uint64_t>(survives.size() - static_cast<size_t>(std::ranges::count(survives, 0)));
  }

  bool Counter::Holdable() const {
    return std::ranges::find(survives, 1) == survives.end();
  }

  namespace {
    bool has(const std::string_view text, const std::string_view part) {
      return text.find(part) != std::string_view::npos;
    }

    bool isHex(const char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

    /// Ruby, PHP, and Perl '...': \\ and \' are escapes, so a backslash before a
    /// backslash, a quote, or the end cannot be written verbatim.
    bool singleQuotedHolds(const std::string_view text) {
      for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\'')
          return false;
        if (text[i] == '\\' && (i + 1 == text.size() || text[i + 1] == '\\' || text[i + 1] == '\''))
          return false;
      }
      return true;
    }

    /// Python r"..." in the literal's quote style: a backslash keeps the next
    /// character but stops it from closing the literal, so the content cannot end in
    /// a lone backslash. Single-quote forms are one line.
    bool pythonRawHolds(const std::string_view text, const std::string_view quote) {
      const bool triple = quote.size() == 3;
      for (size_t i = 0; i < text.size();) {
        if (text[i] == '\\') {
          if (i + 1 == text.size())
            return false;
          i += 2;
          continue;
        }
        if ((!triple && (text[i] == '\n' || text[i] == '\r')) || text.substr(i).starts_with(quote))
          return false;
        ++i;
      }
      return !(triple && text.ends_with(quote[0]));
    }

    /// JavaScript String.raw`...`: a backslash still keeps the next character from
    /// ending the template or starting ${, and CR is normalized to LF.
    bool templateHolds(const std::string_view text) {
      for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\') {
          if (++i == text.size())
            return false;
        } else if (text[i] == '`' || text[i] == '\r' || (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{'))
          return false;
      }
      return true;
    }

    /// Kotlin """...""": templates $name and ${...} still apply.
    bool kotlinRawHolds(const std::string_view text) {
      if (has(text, "\"\"\""))
        return false;
      for (size_t i = 0; i + 1 < text.size(); ++i) {
        const auto next = static_cast<unsigned char>(text[i + 1]);
        if (text[i] == '$' && (std::isalpha(next) || next == '_' || next == '{'))
          return false;
      }
      return true;
    }

    /// Scala """...""": Unicode escapes are still processed (deprecated since 2.13.2).
    bool scalaRawHolds(const std::string_view text) {
      if (has(text, "\"\"\""))
        return false;
      for (size_t i = 0; i + 5 < text.size(); ++i)
        if (text[i] == '\\' && text[i + 1] == 'u' && isHex(text[i + 2]) && isHex(text[i + 3]) &&
            isHex(text[i + 4]) && isHex(text[i + 5]))
          return false;
      return true;
    }

    /// Dart r'...' in the literal's quote style: no escapes; single-quote forms are one line.
    bool dartRawHolds(const std::string_view text, const std::string_view quote) {
      if (quote.size() == 3)
        return !has(text, quote) && !text.ends_with(quote[0]);
      return !has(text, quote) && !has(text, "\n") && !has(text, "\r");
    }
  }

  bool DefaultRawHolds(const LanguageEnum language, const std::string_view content, const std::string_view quote) {
    const std::string_view q = quote.empty() ? "\"" : quote;
    switch (language) {
      case LanguageEnum::CPP: return !has(content, ")\"");                                 // R"(...)"
      case LanguageEnum::Go: return !has(content, "`") && !has(content, "\r");             // `...`
      case LanguageEnum::Python: return pythonRawHolds(content, q);                        // r"..."
      case LanguageEnum::JavaScript:
      case LanguageEnum::TypeScript: return templateHolds(content);                        // String.raw`...`
      case LanguageEnum::CSharp: return !has(content, "\"");                               // @"..."
      case LanguageEnum::Rust: return !has(content, "\"") && !has(content, "\r");          // r"..."
      case LanguageEnum::Ruby:
      case LanguageEnum::PHP:
      case LanguageEnum::Perl: return singleQuotedHolds(content);                          // '...'
      case LanguageEnum::Lua:                                                              // [[...]]
        return !has(content, "]]") && !has(content, "\r") && !content.starts_with('\n');
      case LanguageEnum::Swift:                                                            // #"..."#
        return !has(content, "\"#") && !has(content, "\\#") && !has(content, "\n") && !has(content, "\r");
      case LanguageEnum::Kotlin: return kotlinRawHolds(content);                           // """..."""
      case LanguageEnum::R: return !has(content, ")\"");                                   // r"(...)"
      case LanguageEnum::Scala: return scalaRawHolds(content);                             // """..."""
      case LanguageEnum::OCaml: return !has(content, "|}");                                // {|...|}
      case LanguageEnum::Erlang: return !has(content, "\"");                               // ~S"..."
      case LanguageEnum::Elixir: return !has(content, "\"") && !content.ends_with('\\');   // ~S"..."
      case LanguageEnum::Dart: return dartRawHolds(content, q);                            // r"..."
      default: return false; // C, Objective-C, GLSL, HLSL, Java, Haskell: no raw form
    }
  }

  bool MatchertextHolds(const std::string_view content) {
    return AnalyzeMatcherText(content, false).unmatched == 0;
  }
}
