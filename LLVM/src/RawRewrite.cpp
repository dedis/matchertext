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

  /// C0 and C1 control characters other than tab, line feed, and carriage return.
  /// A raw literal holds them verbatim, but they cannot be typed, so their escape stays.
  bool isControl(const uint32_t cp) {
    return (cp < 0x20 && cp != '\t' && cp != '\n' && cp != '\r') || (cp >= 0x7F && cp <= 0x9F);
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

  /// Code point of a Haskell ASCII control name, or kInvalid.
  uint32_t asciiCode(const std::string_view name) {
    const auto it = std::ranges::find(kAsciiNames, name);
    if (it == kAsciiNames.end())
      return kInvalid;
    const auto index = static_cast<uint32_t>(it - kAsciiNames.begin());
    return index == 33 ? 0x7F : index; // DEL; SP is 0x20 at index 32
  }

  /// Code point of a \N{NAME} escape. Only the names that matter to the rewrite
  /// (backslash, matchers, line breaks, controls) are known; any other name is a
  /// printable character, stood in for by U+FFFD.
  uint32_t namedCode(const std::string_view name) {
    struct Named { std::string_view name; uint32_t cp; };
    static constexpr std::array<Named, 13> kNames = {{
      {"REVERSE SOLIDUS", 0x5C}, {"LEFT PARENTHESIS", 0x28}, {"RIGHT PARENTHESIS", 0x29},
      {"LEFT SQUARE BRACKET", 0x5B}, {"RIGHT SQUARE BRACKET", 0x5D}, {"LEFT CURLY BRACKET", 0x7B},
      {"RIGHT CURLY BRACKET", 0x7D}, {"NULL", 0x00}, {"CHARACTER TABULATION", 0x09},
      {"LINE FEED", 0x0A}, {"CARRIAGE RETURN", 0x0D}, {"SPACE", 0x20}, {"DELETE", 0x7F},
    }};
    std::string upper(name);
    for (char &c: upper)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto &[n, cp]: kNames)
      if (n == upper)
        return cp;
    return 0xFFFD;
  }

  /// Decodes one strict UTF-8 sequence at `i`; returns its length, or 0 if invalid.
  size_t decodeUtf8(const std::string_view bytes, const size_t i, uint32_t &cp) {
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
    content.clear();
    pendingHigh = -1;
  }

  void Counter::push(const uint8_t byte, const int32_t id) {
    bytes.push_back(static_cast<char>(byte));
    owner.push_back(id);
  }

  /// Pushes the UTF-8 encoding of `cp`. A surrogate or an out-of-range value
  /// cannot be written, so its escape keeps its toothpick.
  void Counter::pushCodePoint(const uint32_t cp, const int32_t id) {
    if (cp >= kInvalid || (cp >= 0xD800 && cp <= 0xDFFF)) {
      survives[static_cast<size_t>(id)] = 1;
      return;
    }
    if (cp < 0x80)
      push(static_cast<uint8_t>(cp), id);
    else if (cp < 0x800) {
      push(static_cast<uint8_t>(0xC0 | cp >> 6), id);
      push(static_cast<uint8_t>(0x80 | (cp & 0x3F)), id);
    } else if (cp < 0x10000) {
      push(static_cast<uint8_t>(0xE0 | cp >> 12), id);
      push(static_cast<uint8_t>(0x80 | (cp >> 6 & 0x3F)), id);
      push(static_cast<uint8_t>(0x80 | (cp & 0x3F)), id);
    } else {
      push(static_cast<uint8_t>(0xF0 | cp >> 18), id);
      push(static_cast<uint8_t>(0x80 | (cp >> 12 & 0x3F)), id);
      push(static_cast<uint8_t>(0x80 | (cp >> 6 & 0x3F)), id);
      push(static_cast<uint8_t>(0x80 | (cp & 0x3F)), id);
    }
  }

  /// A high surrogate with no low half after it cannot be written.
  void Counter::flushPending() {
    if (pendingHigh >= 0)
      survives[static_cast<size_t>(pendingHigh)] = 1;
    pendingHigh = -1;
  }

  void Counter::Literal(const std::string_view text) {
    if (text.empty())
      return; // callers pass the empty run between adjacent escapes
    flushPending();
    for (const char c: text)
      push(static_cast<uint8_t>(c), kLiteral);
  }

  bool Counter::decodeEncoding(const std::string_view rest, const int32_t id, const int32_t high, bool &paired) {
    uint32_t value = 0;
    // A byte in byte-string languages, else a code point.
    const auto unit = [&](const uint32_t v) {
      if (dialect.byteEscapes && v <= 0xFF)
        push(static_cast<uint8_t>(v), id);
      else
        pushCodePoint(v, id);
    };
    if (rest.size() >= 2) // Haskell \NUL, \SOH, ...
      if (const uint32_t cp = asciiCode(rest); cp != kInvalid) {
        pushCodePoint(cp, id);
        return true;
      }
    const char c = rest[0];
    switch (c) {
      case 'x':
      case 'o': {
        const uint32_t base = c == 'x' ? 16 : 8;
        if (parseBraced(rest, 1, base, value))
          pushCodePoint(value, id); // Perl \x{263A}, C++23 \x{...} and \o{...}
        else if (rest.size() > 1 && parseDigits(rest, 1, base, value) == rest.size() - 1)
          unit(value);
        else
          return false;
        return true;
      }
      case 'u': {
        if (rest.size() == 5 && parseDigits(rest, 1, 16, value) == 4) {
          // A UTF-16 code unit: a high surrogate waits for its low half.
          if (value >= 0xD800 && value <= 0xDBFF) {
            pendingHigh = id;
            pendingValue = value;
          } else if (value >= 0xDC00 && value <= 0xDFFF && high >= 0) {
            paired = true;
            pushCodePoint(0x10000 + ((pendingValue - 0xD800) << 10) + (value - 0xDC00), high);
          } else
            pushCodePoint(value, id);
          return true;
        }
        // \u{...}, with several space-separated code points in Ruby.
        if (rest.size() < 4 || rest[1] != '{' || rest.back() != '}')
          return false;
        const std::string_view list = rest.substr(2, rest.size() - 3);
        if (list.find_first_not_of("0123456789abcdefABCDEF ") != std::string_view::npos ||
            list.find_first_not_of(' ') == std::string_view::npos)
          return false;
        for (size_t i = 0; i < list.size();) {
          const size_t digits = parseDigits(list, i, 16, value);
          if (digits > 0)
            pushCodePoint(value, id);
          i += std::max<size_t>(digits, 1);
        }
        return true;
      }
      case 'U':
        if (rest.size() != 9 || parseDigits(rest, 1, 16, value) != 8)
          return false;
        pushCodePoint(value, id);
        return true;
      case 'N':
        if (rest.size() <= 3 || rest[1] != '{' || rest.back() != '}')
          return false;
        pushCodePoint(namedCode(rest.substr(2, rest.size() - 3)), id);
        return true;
      case '^': // Haskell and Erlang control characters, \^A is 0x01
        if (rest.size() != 2)
          return false;
        pushCodePoint(rest[1] == '?' ? 0x7F : static_cast<uint32_t>(rest[1]) & 0x1F, id);
        return true;
      default:
        if (c >= '0' && c <= '9' && dialect.digits != Dialect::Digits::None) {
          const uint32_t base = dialect.digits == Dialect::Digits::Octal ? 8 : 10;
          if (parseDigits(rest, 0, base, value) != rest.size())
            return false;
          unit(value);
          return true;
        }
        // Ruby and Perl \cX, \C-x, \M-x. The Ruby grammar marks only \c, \C, or \M
        // and leaves the rest as content. A control character keeps its toothpick,
        // and a meta character is a byte above 0x7F, which alone is not valid UTF-8.
        if (!dialect.controlLetters || !(c == 'c' || ((c == 'C' || c == 'M') && (rest.size() == 1 || rest[1] == '-'))))
          return false;
        if (const size_t at = c == 'c' ? 1 : 2; rest.size() <= at)
          survives[static_cast<size_t>(id)] = c == 'M' ? 1 : 3;
        else if (c == 'M')
          push(static_cast<uint8_t>(0x80 | (static_cast<uint8_t>(rest[at]) & 0x7F)), id);
        else
          push(static_cast<uint8_t>(rest[at] == '?' ? 0x7F : rest[at] & 0x1F), id);
        return true;
    }
  }

  void Counter::Escape(const std::string_view text) {
    const auto id = static_cast<int32_t>(survives.size());
    survives.push_back(0);
    const int32_t high = pendingHigh;
    pendingHigh = -1;
    bool paired = false;

    const std::string_view rest = text.substr(1);
    // A dangling backslash, a line continuation, or a Haskell string gap decodes to nothing.
    const bool nothing = rest.empty() || rest[0] == ' ' || rest[0] == '\t' || rest[0] == '\n' ||
                         rest[0] == '\r' || dialect.empty.find(rest[0]) != std::string_view::npos;
    if (!nothing && !decodeEncoding(rest, id, high, paired)) {
      const char c = rest[0];
      if (dialect.controls.find(c) != std::string_view::npos)
        push(static_cast<uint8_t>(controlCode(c)), id);
      else if (dialect.unknownKeepsBackslash && c != '\\' && c != '"' && c != '\'' && c != '$')
        for (const char r: text) // an unknown escape is kept as written, backslash included
          push(static_cast<uint8_t>(r), kLiteral);
      else
        for (const char r: rest) // identity escape: decodes to the escaped character itself
          push(static_cast<uint8_t>(r), id);
    }

    if (high >= 0 && !paired)
      survives[static_cast<size_t>(high)] = 1;
  }

  uint64_t Counter::Count() {
    flushPending();
    content.clear();
    uint64_t literalBackslashes = 0;
    for (size_t i = 0; i < bytes.size();) {
      uint32_t cp = 0;
      const size_t len = decodeUtf8(bytes, i, cp);
      const int32_t id = owner[i];
      if (len == 0) {
        if (id >= 0)
          survives[static_cast<size_t>(id)] = 1; // not valid UTF-8, so it cannot be written
        content.push_back(bytes[i++]);
        continue;
      }
      if (id == kLiteral)
        literalBackslashes += cp == kBackslash;
      else if (cp == kBackslash) {
        if (survives[static_cast<size_t>(id)] == 0)
          survives[static_cast<size_t>(id)] = 2;
      } else if (isControl(cp) && survives[static_cast<size_t>(id)] == 0)
        survives[static_cast<size_t>(id)] = 3;
      content.append(bytes, i, len);
      i += len;
    }
    return literalBackslashes + static_cast<uint64_t>(survives.size() - static_cast<size_t>(std::ranges::count(survives, 0)));
  }

  bool Counter::Holdable() const {
    return std::ranges::find(survives, 1) == survives.end();
  }

  bool HasControl(const std::string_view content) {
    for (size_t i = 0; i < content.size();) {
      uint32_t cp = 0;
      const size_t len = decodeUtf8(content, i, cp);
      if (len > 0 && isControl(cp))
        return true;
      i += std::max<size_t>(len, 1);
    }
    return false;
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

  namespace {
    using Kind = RawFormCheck::Kind;

    constexpr RawFormCheck kCpp[] = {
      {R"x(R"(...)")x", R"x(contains )")x", Kind::Contains, R"x()")x"},
    };
    constexpr RawFormCheck kGo[] = {
      {"`...`", "contains `", Kind::Contains, "`"},
      {"`...`", "contains a CR", Kind::CarriageReturn},
    };
    constexpr RawFormCheck kPython[] = {
      {R"(r"...")", R"(contains an unescaped ")", Kind::PyQuote, R"(")"},
      {R"(r"...")", "contains an unescaped line break", Kind::PyLineBreak},
      {R"(r"...")", "ends in an odd run of backslashes", Kind::OddBackslashes},
      {"r'...'", "contains an unescaped '", Kind::PyQuote, "'"},
      {"r'...'", "contains an unescaped line break", Kind::PyLineBreak},
      {"r'...'", "ends in an odd run of backslashes", Kind::OddBackslashes},
      {R"(r"""...""")", R"(contains an unescaped """ or ends in ")", Kind::PyQuote, R"(""")"},
      {R"(r"""...""")", "ends in an odd run of backslashes", Kind::OddBackslashes},
      {"r'''...'''", "contains an unescaped ''' or ends in '", Kind::PyQuote, "'''"},
      {"r'''...'''", "ends in an odd run of backslashes", Kind::OddBackslashes},
    };
    constexpr RawFormCheck kTemplate[] = {
      {"String.raw`...`", "contains an unescaped `", Kind::TemplateQuote},
      {"String.raw`...`", "contains an unescaped ${", Kind::TemplateHole},
      {"String.raw`...`", "contains an unescaped CR", Kind::TemplateCR},
      {"String.raw`...`", "ends in an odd run of backslashes", Kind::OddBackslashes},
    };
    constexpr RawFormCheck kCSharp[] = {
      {R"(@"...")", R"(contains ")", Kind::Contains, R"(")"},
    };
    constexpr RawFormCheck kRust[] = {
      {R"(r"...")", R"(contains ")", Kind::Contains, R"(")"},
      {R"(r"...")", "contains a CR", Kind::CarriageReturn},
      {R"(r#"..."#)", R"(contains "#)", Kind::Contains, R"("#)"},
      {R"(r#"..."#)", "contains a CR", Kind::CarriageReturn},
      {R"(r##"..."##)", R"(contains "##)", Kind::Contains, R"("##)"},
      {R"(r##"..."##)", "contains a CR", Kind::CarriageReturn},
      {R"(r###"..."###)", R"(contains "###)", Kind::Contains, R"("###)"},
      {R"(r###"..."###)", "contains a CR", Kind::CarriageReturn},
    };
    constexpr RawFormCheck kSingleQuoted[] = {
      {"'...'", "contains '", Kind::Contains, "'"},
      {"'...'", R"(has a backslash before \, ' or the end)", Kind::SingleQuoted},
    };
    constexpr RawFormCheck kLua[] = {
      {"[[...]]", "contains ]]", Kind::Contains, "]]"},
      {"[[...]]", "contains a CR", Kind::CarriageReturn},
      {"[[...]]", "starts with a line feed", Kind::LeadingNewline},
    };
    constexpr RawFormCheck kSwift[] = {
      {R"(#"..."#)", R"(contains "#)", Kind::Contains, R"("#)"},
      {R"(#"..."#)", R"(contains \#)", Kind::Contains, R"(\#)"},
      {R"(#"..."#)", "contains a line break", Kind::LineBreak},
    };
    constexpr RawFormCheck kKotlin[] = {
      {R"("""...""")", R"(contains """)", Kind::Contains, R"(""")"},
      {R"("""...""")", "contains a $ template", Kind::KotlinTemplate},
    };
    constexpr RawFormCheck kR[] = {
      {R"x(r"(...)")x", R"x(contains )")x", Kind::Contains, R"x()")x"},
    };
    constexpr RawFormCheck kScala[] = {
      {R"("""...""")", R"(contains """)", Kind::Contains, R"(""")"},
      {R"("""...""")", R"(contains a \uXXXX escape)", Kind::ScalaUnicode},
    };
    constexpr RawFormCheck kOCaml[] = {
      {"{|...|}", "contains |}", Kind::Contains, "|}"},
    };
    constexpr RawFormCheck kErlang[] = {
      {R"(~S"...")", R"(contains ")", Kind::Contains, R"(")"},
    };
    constexpr RawFormCheck kElixir[] = {
      {R"(~S"...")", R"(contains ")", Kind::Contains, R"(")"},
      {R"(~S"...")", R"(ends with \)", Kind::EndsWith, R"(\)"},
    };
    constexpr RawFormCheck kDart[] = {
      {R"(r"...")", R"(contains ")", Kind::DartQuote, R"(")"},
      {R"(r"...")", "contains a line break", Kind::LineBreak},
      {"r'...'", "contains '", Kind::DartQuote, "'"},
      {"r'...'", "contains a line break", Kind::LineBreak},
      {R"(r"""...""")", R"(contains """ or ends in ")", Kind::DartQuote, R"(""")"},
      {"r'''...'''", "contains ''' or ends in '", Kind::DartQuote, "'''"},
    };

    /// Python raw literals: a backslash keeps the next character from closing the
    /// literal. True if an unescaped `quote` (or, with `lineBreak`, an unescaped CR
    /// or LF) occurs.
    bool pythonFinds(const std::string_view text, const std::string_view quote, const bool lineBreak) {
      for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\')
          ++i;
        else if (lineBreak ? text[i] == '\n' || text[i] == '\r' : text.substr(i).starts_with(quote))
          return true;
      }
      return false;
    }

    /// String.raw: true if an unescaped character for which `stop` holds occurs.
    template <typename Stop>
    bool templateFinds(const std::string_view text, Stop stop) {
      for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\')
          ++i;
        else if (stop(i))
          return true;
      }
      return false;
    }

    bool oddBackslashes(const std::string_view text) {
      const size_t last = text.find_last_not_of('\\');
      return (text.size() - (last == std::string_view::npos ? 0 : last + 1)) % 2 == 1;
    }

    bool violated(const RawFormCheck &check, const std::string_view text) {
      switch (check.kind) {
        case Kind::Contains: return has(text, check.needle);
        case Kind::EndsWith: return text.ends_with(check.needle);
        case Kind::CarriageReturn: return has(text, "\r");
        case Kind::LineBreak: return has(text, "\n") || has(text, "\r");
        case Kind::LeadingNewline: return text.starts_with('\n');
        case Kind::PyQuote:
          return pythonFinds(text, check.needle, false) || (check.needle.size() == 3 && text.ends_with(check.needle[0]));
        case Kind::PyLineBreak: return pythonFinds(text, {}, true);
        case Kind::OddBackslashes: return oddBackslashes(text);
        case Kind::DartQuote:
          return has(text, check.needle) || (check.needle.size() == 3 && text.ends_with(check.needle[0]));
        case Kind::TemplateQuote: return templateFinds(text, [&](const size_t i) { return text[i] == '`'; });
        case Kind::TemplateHole:
          return templateFinds(text, [&](const size_t i) { return text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{'; });
        case Kind::TemplateCR: return templateFinds(text, [&](const size_t i) { return text[i] == '\r'; });
        case Kind::SingleQuoted: return !singleQuotedHolds(text) && !has(text, "'");
        case Kind::KotlinTemplate: return !kotlinRawHolds(text) && !has(text, R"(""")");
        case Kind::ScalaUnicode: return !scalaRawHolds(text) && !has(text, R"(""")");
      }
      return false;
    }
  }

  std::span<const RawFormCheck> RawFormChecks(const LanguageEnum language) {
    switch (language) {
      case LanguageEnum::CPP: return kCpp;
      case LanguageEnum::Go: return kGo;
      case LanguageEnum::Python: return kPython;
      case LanguageEnum::JavaScript:
      case LanguageEnum::TypeScript: return kTemplate;
      case LanguageEnum::CSharp: return kCSharp;
      case LanguageEnum::Rust: return kRust;
      case LanguageEnum::Ruby:
      case LanguageEnum::PHP:
      case LanguageEnum::Perl: return kSingleQuoted;
      case LanguageEnum::Lua: return kLua;
      case LanguageEnum::Swift: return kSwift;
      case LanguageEnum::Kotlin: return kKotlin;
      case LanguageEnum::R: return kR;
      case LanguageEnum::Scala: return kScala;
      case LanguageEnum::OCaml: return kOCaml;
      case LanguageEnum::Erlang: return kErlang;
      case LanguageEnum::Elixir: return kElixir;
      case LanguageEnum::Dart: return kDart;
      default: return {}; // C, Objective-C, GLSL, HLSL, Java, Haskell: no raw form
    }
  }

  uint64_t RawFormViolations(const LanguageEnum language, const std::string_view content) {
    uint64_t mask = 0;
    const auto checks = RawFormChecks(language);
    for (size_t i = 0; i < checks.size(); ++i)
      if (violated(checks[i], content))
        mask |= uint64_t{1} << i;
    return mask;
  }

  std::vector<std::string_view> RawForms(const LanguageEnum language) {
    std::vector<std::string_view> forms;
    for (const auto &check: RawFormChecks(language))
      if (forms.empty() || forms.back() != check.form)
        forms.push_back(check.form);
    return forms;
  }

  namespace {
    constexpr RawFormFamily kFamilies[] = {
      {"C++", kCpp}, {"Go", kGo}, {"Python", kPython}, {"JavaScript/TypeScript", kTemplate},
      {"C#", kCSharp}, {"Rust", kRust}, {"Ruby/PHP/Perl", kSingleQuoted}, {"Lua", kLua},
      {"Swift", kSwift}, {"Kotlin", kKotlin}, {"R", kR}, {"Scala", kScala}, {"OCaml", kOCaml},
      {"Erlang", kErlang}, {"Elixir", kElixir}, {"Dart", kDart},
    };

    /// Calls `visit(family, first check, end check, form number)` for every form of
    /// every family, with checks and forms numbered across families.
    template <typename Visit>
    void forEachForm(Visit visit) {
      size_t check = 0, form = 0;
      for (const auto &family: kFamilies)
        for (size_t i = 0; i < family.checks.size(); ++form) {
          const size_t first = i;
          while (i < family.checks.size() && family.checks[i].form == family.checks[first].form)
            ++i;
          visit(family, check + first, check + i, form);
          if (i == family.checks.size())
            check += i;
        }
    }
  }

  std::span<const RawFormFamily> RawFormFamilies() {
    return kFamilies;
  }

  uint64_t AllRawFormViolations(const std::string_view content) {
    uint64_t mask = 0;
    size_t bit = 0;
    for (const auto &family: kFamilies)
      for (const auto &check: family.checks)
        mask |= static_cast<uint64_t>(violated(check, content)) << bit++;
    return mask;
  }

  uint64_t AllRawFormsHolding(const uint64_t violations, const bool encodable) {
    if (!encodable)
      return 0;
    uint64_t holding = 0;
    forEachForm([&](const RawFormFamily &, const size_t first, const size_t end, const size_t form) {
      const uint64_t checks = (end - first == 64 ? ~uint64_t{0} : (uint64_t{1} << (end - first)) - 1) << first;
      if ((violations & checks) == 0)
        holding |= uint64_t{1} << form;
    });
    return holding;
  }

  uint64_t OwnRawForms(const LanguageEnum language) {
    const auto own = RawFormChecks(language);
    uint64_t mask = 0;
    forEachForm([&](const RawFormFamily &family, size_t, size_t, const size_t form) {
      if (!own.empty() && family.checks.data() == own.data())
        mask |= uint64_t{1} << form;
    });
    return mask;
  }

  uint64_t RawFormsHolding(const LanguageEnum language, const uint64_t violations, const bool encodable) {
    if (!encodable)
      return 0;
    const auto checks = RawFormChecks(language);
    uint64_t holding = 0;
    for (size_t i = 0, form = 0; i < checks.size(); ++form) {
      bool holds = true;
      const std::string_view name = checks[i].form;
      for (; i < checks.size() && checks[i].form == name; ++i)
        holds = holds && !(violations >> i & 1);
      if (holds)
        holding |= uint64_t{1} << form;
    }
    return holding;
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
