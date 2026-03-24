//
// LanguageClassifier.cpp
// Author: Antoine Bastide
// Date: 24/03/2026
//

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include "../include/LanguageClassifier.hpp"
#include "../include/LanguageModel.generated.hpp"

// ============================================================================
// Language name table
// ============================================================================

const char *LanguageName(const Language lang) {
  static constexpr const char *names[] = {
      "Unknown",     "PlainText",  "URL",        "FilePath",    "FormatString",
      "SQL",         "HTML",       "XML",        "JSON",        "YAML",
      "CSS",         "Regex",      "Shell",      "Python",      "JavaScript",
      "TypeScript",  "Java",       "C",          "C++",         "C#",
      "Go",          "Rust",       "Ruby",       "PHP",         "Perl",
      "Lua",         "Swift",      "Kotlin",     "R",           "Scala",
      "Haskell",     "OCaml",      "Erlang",     "Elixir",      "Dart",
      "Objective-C", "GLSL",       "HLSL",       "IdentifierLike",
      "HexData",     "BinaryData",
  };
  static_assert(std::size(names) == static_cast<size_t>(Language::COUNT));
  const auto idx = static_cast<size_t>(lang);
  return idx < std::size(names) ? names[idx] : "Unknown";
}

// ============================================================================
// Helpers
// ============================================================================

static char ToLower(const char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

static std::string_view TrimLeft(const std::string_view s) {
  const size_t pos = s.find_first_not_of(" \t\n\r");
  return pos == std::string_view::npos ? std::string_view{} : s.substr(pos);
}

static std::string_view TrimRight(const std::string_view s) {
  const size_t pos = s.find_last_not_of(" \t\n\r");
  return pos == std::string_view::npos ? std::string_view{} : s.substr(0, pos + 1);
}

static std::string_view Trim(const std::string_view s) {
  return TrimRight(TrimLeft(s));
}

/// Case-insensitive prefix check.
static bool StartsWithCI(const std::string_view s, const std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); i++) {
    if (ToLower(s[i]) != ToLower(prefix[i])) return false;
  }
  return true;
}

/// Case-insensitive substring search. Returns position or npos.
static size_t FindCI(const std::string_view haystack, const std::string_view needle,
                     const size_t pos = 0) {
  if (needle.empty()) return pos;
  if (needle.size() > haystack.size()) return std::string_view::npos;
  const size_t last = haystack.size() - needle.size();
  for (size_t i = pos; i <= last; i++) {
    bool found = true;
    for (size_t j = 0; j < needle.size(); j++) {
      if (ToLower(haystack[i + j]) != ToLower(needle[j])) {
        found = false;
        break;
      }
    }
    if (found) return i;
  }
  return std::string_view::npos;
}

static std::string StripCommentDecorators(const std::string_view body) {
  std::string cleaned;
  cleaned.reserve(body.size());

  size_t pos = 0;
  bool wroteLine = false;
  while (pos <= body.size()) {
    const size_t end = body.find('\n', pos);
    const size_t lineEnd = end == std::string_view::npos ? body.size() : end;
    auto line = Trim(body.substr(pos, lineEnd - pos));

    if (line.starts_with("/*")) line = TrimLeft(line.substr(2));
    if (line.ends_with("*/")) line = TrimRight(line.substr(0, line.size() - 2));
    if (line.starts_with("///"))
      line = TrimLeft(line.substr(3));
    else if (line.starts_with("//"))
      line = TrimLeft(line.substr(2));
    else if (line.starts_with('*'))
      line = TrimLeft(line.substr(1));
    else if ((line.starts_with("# ") || line.starts_with("#\t")) &&
             !line.starts_with("#!"))
      line = TrimLeft(line.substr(1));
    else if (line.starts_with("<!--"))
      line = TrimLeft(line.substr(4));

    if (line.ends_with("-->"))
      line = TrimRight(line.substr(0, line.size() - 3));

    if (!line.empty()) {
      if (wroteLine) cleaned.push_back('\n');
      cleaned.append(line);
      wroteLine = true;
    }

    if (end == std::string_view::npos) break;
    pos = end + 1;
  }

  if (!cleaned.empty()) return cleaned;
  return std::string(Trim(body));
}

static std::string UnescapeCommonSequences(const std::string_view body) {
  std::string result;
  result.reserve(body.size());

  for (size_t i = 0; i < body.size(); i++) {
    if (body[i] != '\\' || i + 1 >= body.size()) {
      result.push_back(body[i]);
      continue;
    }

    const char next = body[i + 1];
    switch (next) {
      case '\\': result.push_back('\\'); i++; break;
      case '"': result.push_back('"'); i++; break;
      case '\'': result.push_back('\''); i++; break;
      case 'n': result.push_back('\n'); i++; break;
      case 'r': result.push_back('\r'); i++; break;
      case 't': result.push_back('\t'); i++; break;
      default: result.push_back(body[i]); break;
    }
  }

  return result;
}

static std::string NormalizeForClassification(const std::string_view body) {
  return UnescapeCommonSequences(StripCommentDecorators(body));
}

static bool IsSimpleKey(const std::string_view s) {
  if (s.empty()) return false;
  for (const char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) &&
        c != '_' && c != '-' && c != '.')
      return false;
  }
  return true;
}

static int CountWords(const std::string_view s) {
  int words = 0;
  bool inWord = false;
  for (const char c : s) {
    const bool isWord =
        std::isalnum(static_cast<unsigned char>(c)) || c == '\'' || c == '-';
    if (isWord && !inWord) words++;
    inWord = isWord;
  }
  return words;
}

static bool IsIdentifierLikeSeparator(const char c) {
  return c == '_' || c == '-' || c == '.' || c == '/' || c == ':';
}

static bool IsHexDigit(const char c) {
  return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

static bool IsOctalDigit(const char c) {
  return c >= '0' && c <= '7';
}

/// Check whether a tag name is a known HTML5 element (case-insensitive).
static bool IsHTMLTagName(const std::string_view name) {
  if (name.empty() || name.size() > 10) return false;

  // Lowercase the tag name into a small stack buffer.
  char lower[11];
  for (size_t i = 0; i < name.size(); i++)
    lower[i] = ToLower(name[i]);
  const std::string_view lname(lower, name.size());

  // Sorted list of common HTML5 element names.
  static constexpr std::string_view tags[] = {
      "a",       "abbr",     "address",  "article",  "aside",    "b",
      "body",    "br",       "button",   "canvas",   "caption",  "code",
      "col",     "dd",       "details",  "div",      "dl",       "dt",
      "em",      "fieldset", "figure",   "footer",   "form",     "h1",
      "h2",      "h3",       "h4",       "h5",       "h6",       "head",
      "header",  "hr",       "html",     "i",        "iframe",   "img",
      "input",   "label",    "li",       "link",     "main",     "meta",
      "nav",     "ol",       "option",   "p",        "pre",      "script",
      "section", "select",   "small",    "span",     "strong",   "style",
      "summary", "svg",      "table",    "tbody",    "td",       "template",
      "textarea","tfoot",    "th",       "thead",    "title",    "tr",
      "u",       "ul",       "video",
  };
  return std::binary_search(std::begin(tags), std::end(tags), lname);
}

/// Pack three bytes into a uint32_t trigram key.
static uint32_t PackTrigram(const char a, const char b, const char c) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(c));
}

static const std::array<uint16_t, 64> &ModelTrigramCounts() {
  static const std::array<uint16_t, 64> counts = [] {
    std::array<uint16_t, 64> values{};
    static_assert(kNumLanguages <= values.size(), "Increase model trigram count buffer");
    for (size_t i = 0; i < kNumCombinedEntries; i++) {
      if (kCombinedEntries[i].languageIdx < values.size())
        values[kCombinedEntries[i].languageIdx]++;
    }
    return values;
  }();
  return counts;
}

// ============================================================================
// Layer 1: Structural detectors
// ============================================================================

/// Detect URLs by scheme prefix.
static ClassificationResult DetectURL(const std::string_view s) {
  static constexpr std::string_view schemes[] = {
      "http://", "https://", "ftp://",    "ftps://", "file://",
      "mailto:", "ssh://",   "git://",    "svn://",  "telnet://",
      "ws://",   "wss://",   "data:",
  };
  for (const auto scheme : schemes) {
    if (s.size() > scheme.size() && s.starts_with(scheme))
      return {Language::URL, 0.95f};
  }
  return {Language::Unknown, 0.0f};
}

/// Detect file paths by prefix patterns and separator density.
static ClassificationResult DetectFilePath(const std::string_view s) {
  if (s.size() < 2) return {Language::Unknown, 0.0f};

  // Windows absolute: C:\ or C:/
  if (s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) &&
      s[1] == ':' && (s[2] == '\\' || s[2] == '/'))
    return {Language::FilePath, 0.85f};

  // Unix absolute
  if (s[0] == '/' && s[1] != '*' && s[1] != '/') {
    int slashes = 0;
    for (const char c : s)
      if (c == '/') slashes++;
    if (slashes >= 2) return {Language::FilePath, 0.80f};
    return {Language::FilePath, 0.55f};
  }

  // Relative: ./ or ../
  if (s.starts_with("./") || s.starts_with("../"))
    return {Language::FilePath, 0.75f};

  return {Language::Unknown, 0.0f};
}

/// Detect printf-style format strings by counting specifiers.
static ClassificationResult DetectFormatString(const std::string_view s) {
  int specs = 0;
  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (s[i] != '%') continue;
    if (s[i + 1] == '%') { i++; continue; } // escaped %%
    const char c = s[i + 1];
    if (c == 'd' || c == 'i' || c == 'u' || c == 'f' || c == 'e' ||
        c == 'g' || c == 'x' || c == 'X' || c == 'o' || c == 's' ||
        c == 'c' || c == 'p' || c == 'l' || c == 'z' || c == 'h') {
      specs++;
      i++;
    }
  }
  if (specs == 0) return {Language::Unknown, 0.0f};
  const float density = static_cast<float>(specs) / static_cast<float>(s.size());
  return {Language::FormatString, std::min(0.5f + density * 10.0f, 0.95f)};
}

/// Detect escaped-byte blobs or strings containing many non-printable bytes.
static ClassificationResult DetectBinaryData(const std::string_view s) {
  if (s.size() < 8) return {Language::Unknown, 0.0f};

  int controlBytes = 0;
  int escapedBytes = 0;
  int hexEscapes = 0;
  int octalEscapes = 0;
  int nullEscapes = 0;
  int letters = 0;
  int spaces = 0;

  for (size_t i = 0; i < s.size(); i++) {
    const unsigned char uc = static_cast<unsigned char>(s[i]);
    if (std::isalpha(uc)) letters++;
    if (std::isspace(uc)) spaces++;
    if (uc < 0x20 && s[i] != '\n' && s[i] != '\r' && s[i] != '\t')
      controlBytes++;

    if (s[i] != '\\' || i + 1 >= s.size())
      continue;

    const char next = s[i + 1];
    if (next == 'x' || next == 'X') {
      size_t j = i + 2;
      int digits = 0;
      while (j < s.size() && IsHexDigit(s[j])) {
        digits++;
        j++;
      }
      if (digits >= 2) {
        escapedBytes++;
        hexEscapes++;
        i = j - 1;
        continue;
      }
    }

    if (next == '0') {
      size_t j = i + 1;
      int digits = 0;
      while (j < s.size() && digits < 3 && IsOctalDigit(s[j])) {
        digits++;
        j++;
      }
      escapedBytes++;
      nullEscapes++;
      if (digits > 1) octalEscapes++;
      i = j - 1;
      continue;
    }

    if (IsOctalDigit(next)) {
      size_t j = i + 1;
      int digits = 0;
      while (j < s.size() && digits < 3 && IsOctalDigit(s[j])) {
        digits++;
        j++;
      }
      if (digits >= 2) {
        escapedBytes++;
        octalEscapes++;
        i = j - 1;
        continue;
      }
    }
  }

  const int strongEscapes = hexEscapes + octalEscapes + nullEscapes;
  const float controlRatio =
      static_cast<float>(controlBytes) / static_cast<float>(std::max<size_t>(s.size(), 1));
  const float escapeRatio =
      static_cast<float>(escapedBytes * 4) / static_cast<float>(std::max<size_t>(s.size(), 1));

  if (controlBytes >= 4 && controlRatio >= 0.08f)
    return {Language::BinaryData, std::min(0.72f + controlRatio, 0.96f)};

  if (strongEscapes >= 4 && letters <= 8 && spaces == 0)
    return {Language::BinaryData,
            std::min(0.76f + static_cast<float>(strongEscapes) * 0.03f, 0.97f)};

  if (hexEscapes >= 3 && escapeRatio >= 0.40f)
    return {Language::BinaryData,
            std::min(0.74f + static_cast<float>(hexEscapes) * 0.04f, 0.96f)};

  if ((octalEscapes + nullEscapes) >= 4 && escapeRatio >= 0.35f)
    return {Language::BinaryData,
            std::min(0.74f + static_cast<float>(octalEscapes + nullEscapes) * 0.04f, 0.96f)};

  return {Language::Unknown, 0.0f};
}

/// Detect long hex strings and grouped hex payloads.
static ClassificationResult DetectHexData(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 16) return {Language::Unknown, 0.0f};

  int hexDigits = 0;
  int separators = 0;
  for (const char c : trimmed) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isxdigit(uc)) {
      hexDigits++;
      continue;
    }
    if (c == ' ' || c == ':' || c == '-' || c == '_') {
      separators++;
      continue;
    }
    return {Language::Unknown, 0.0f};
  }

  const int significant = hexDigits + separators;
  if (hexDigits < 16 || significant == 0)
    return {Language::Unknown, 0.0f};

  const float hexRatio =
      static_cast<float>(hexDigits) / static_cast<float>(significant);
  if (hexRatio < 0.85f)
    return {Language::Unknown, 0.0f};

  const bool evenDigits = (hexDigits % 2) == 0;
  if (!evenDigits && separators == 0)
    return {Language::Unknown, 0.0f};

  return {Language::HexData,
          std::min(0.75f + static_cast<float>(hexDigits) / 128.0f, 0.97f)};
}

/// Detect JSON by leading brace/bracket and "key": patterns.
static ClassificationResult DetectJSON(const std::string_view s) {
  const auto trimmed = TrimLeft(s);
  if (trimmed.empty()) return {Language::Unknown, 0.0f};
  if (trimmed[0] != '{' && trimmed[0] != '[')
    return {Language::Unknown, 0.0f};

  // Count "key": patterns
  int pairs = 0;
  for (size_t i = 0; i + 2 < s.size(); i++) {
    if (s[i] != '"') continue;
    const size_t close = s.find('"', i + 1);
    if (close == std::string_view::npos) break;
    const size_t next = s.find_first_not_of(" \t\n\r", close + 1);
    if (next != std::string_view::npos && s[next] == ':') {
      pairs++;
      i = next;
    } else {
      i = close;
    }
  }
  if (pairs == 0) return {Language::Unknown, 0.0f};
  return {Language::JSON, std::min(0.6f + pairs * 0.1f, 0.95f)};
}

/// Detect YAML by repeated "key: value" lines or list items.
static ClassificationResult DetectYAML(const std::string_view s) {
  int keyValueLines = 0;
  int listLines = 0;
  int nonEmptyLines = 0;

  size_t pos = 0;
  while (pos <= s.size()) {
    const size_t end = s.find('\n', pos);
    const size_t lineEnd = end == std::string_view::npos ? s.size() : end;
    const auto line = Trim(s.substr(pos, lineEnd - pos));

    if (!line.empty()) {
      nonEmptyLines++;
      if (line == "---" || line == "...") {
        keyValueLines++;
      } else if (line.starts_with("- ")) {
        listLines++;
      } else {
        const size_t colon = line.find(':');
        if (colon != std::string_view::npos && colon > 0 && colon + 1 < line.size()) {
          const auto key = TrimRight(line.substr(0, colon));
          const auto value = TrimLeft(line.substr(colon + 1));
          if (IsSimpleKey(key) && !value.empty() &&
              line.find('{') == std::string_view::npos &&
              line.find('}') == std::string_view::npos &&
              line.find(';') == std::string_view::npos) {
            keyValueLines++;
          }
        }
      }
    }

    if (end == std::string_view::npos) break;
    pos = end + 1;
  }

  const int signals = keyValueLines + listLines;
  if (signals < 2) return {Language::Unknown, 0.0f};
  if (nonEmptyLines == 1 && keyValueLines < 2) return {Language::Unknown, 0.0f};
  return {Language::YAML, std::min(0.58f + signals * 0.08f, 0.92f)};
}

/// Detect SQL by leading keyword and supporting keywords.
static ClassificationResult DetectSQL(const std::string_view s) {
  const auto trimmed = TrimLeft(s);
  if (trimmed.size() < 6) return {Language::Unknown, 0.0f};

  static constexpr std::string_view leaders[] = {
      "select ", "insert ", "update ", "delete ", "create ",
      "alter ",  "drop ",   "merge ",  "with ",   "grant ",
      "revoke ", "begin ",  "commit ", "rollback ", "explain ",
  };
  bool hasLeader = false;
  for (const auto leader : leaders) {
    if (StartsWithCI(trimmed, leader)) { hasLeader = true; break; }
  }
  if (!hasLeader) return {Language::Unknown, 0.0f};

  // Count supporting keywords for confidence
  static constexpr std::string_view keywords[] = {
      " from ",  " where ", " join ",   " inner ",  " outer ",
      " left ",  " right ", " group ",  " order ",  " having ",
      " limit ", " values", " into ",   " set ",    " table ",
      " index ", " on ",    " and ",    " or ",     " not ",
      " in ",    " like ",  " between "," exists ",  " null",
      " as ",    " distinct"," union ",
  };
  int kwCount = 0;
  for (const auto kw : keywords) {
    if (FindCI(s, kw) != std::string_view::npos)
      kwCount++;
  }
  return {Language::SQL, std::min(0.65f + kwCount * 0.05f, 0.95f)};
}

/// Detect HTML by scanning for known HTML5 tag names in angle brackets.
static ClassificationResult DetectHTML(const std::string_view s) {
  // Fast reject: no angle brackets at all
  if (s.find('<') == std::string_view::npos)
    return {Language::Unknown, 0.0f};

  // DOCTYPE is definitive
  if (FindCI(s, "<!doctype") != std::string_view::npos)
    return {Language::HTML, 0.95f};

  int htmlTags = 0;
  int closingTags = 0;

  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '<') continue;

    const bool isClosing = (i + 1 < s.size() && s[i + 1] == '/');
    const size_t nameStart = i + 1 + (isClosing ? 1 : 0);
    if (nameStart >= s.size() ||
        !std::isalpha(static_cast<unsigned char>(s[nameStart])))
      continue;

    size_t nameEnd = nameStart;
    while (nameEnd < s.size() &&
           std::isalnum(static_cast<unsigned char>(s[nameEnd])))
      nameEnd++;

    if (nameEnd == nameStart) continue;
    if (nameEnd < s.size()) {
      const char next = s[nameEnd];
      if (next != ' ' && next != '>' && next != '/' &&
          next != '\t' && next != '\n')
        continue;
    }

    if (IsHTMLTagName(s.substr(nameStart, nameEnd - nameStart))) {
      htmlTags++;
      if (isClosing) closingTags++;
    }
  }

  if (htmlTags == 0) return {Language::Unknown, 0.0f};
  float confidence = 0.6f;
  if (closingTags > 0) confidence += 0.15f;
  confidence += std::min(htmlTags * 0.05f, 0.20f);
  return {Language::HTML, std::min(confidence, 0.95f)};
}

/// Detect XML by prolog, CDATA, or namespaced tags.
static ClassificationResult DetectXML(const std::string_view s) {
  if (s.find("<?xml") != std::string_view::npos)
    return {Language::XML, 0.95f};
  if (s.find("<![CDATA[") != std::string_view::npos)
    return {Language::XML, 0.90f};

  // Look for namespaced tags: <ns:tag
  int nsTags = 0;
  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (s[i] != '<' || s[i + 1] == '/' || s[i + 1] == '!' || s[i + 1] == '?')
      continue;
    const size_t tagEnd = s.find_first_of("> \t\n/", i + 1);
    if (tagEnd == std::string_view::npos) continue;
    const auto tagName = s.substr(i + 1, tagEnd - i - 1);
    if (tagName.find(':') != std::string_view::npos)
      nsTags++;
    i = tagEnd;
  }
  if (nsTags == 0) return {Language::Unknown, 0.0f};
  return {Language::XML, std::min(0.7f + nsTags * 0.1f, 0.95f)};
}

/// Detect regex patterns by counting metacharacter signals.
static ClassificationResult DetectRegex(const std::string_view s) {
  if (s.size() < 3) return {Language::Unknown, 0.0f};

  int signals = 0;

  // Regex-specific escape sequences
  static constexpr std::string_view escapes[] = {
      "\\d", "\\D", "\\w", "\\W", "\\s", "\\S", "\\b", "\\B",
  };
  for (const auto esc : escapes) {
    if (s.find(esc) != std::string_view::npos) signals += 2;
  }

  // Regex-specific groups
  static constexpr std::string_view groups[] = {
      "(?:", "(?=", "(?!", "(?<=", "(?<!", "(?P<", "(?P=",
  };
  for (const auto grp : groups) {
    if (s.find(grp) != std::string_view::npos) signals += 3;
  }

  // Character classes [...]
  if (s.find('[') != std::string_view::npos &&
      s.find(']') != std::string_view::npos)
    signals++;

  // Quantifiers {n} or {n,m}
  for (size_t i = 0; i + 2 < s.size(); i++) {
    if (s[i] == '{' && std::isdigit(static_cast<unsigned char>(s[i + 1]))) {
      size_t j = i + 1;
      while (j < s.size() &&
             (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == ','))
        j++;
      if (j < s.size() && s[j] == '}') signals += 2;
    }
  }

  // Anchors
  if (s.front() == '^') signals++;
  if (s.back() == '$') signals++;

  if (signals < 2) return {Language::Unknown, 0.0f};
  return {Language::Regex, std::min(0.5f + signals * 0.08f, 0.95f)};
}

/// Detect CSS by scanning for known CSS property names.
static ClassificationResult DetectCSS(const std::string_view s) {
  // Fast reject: CSS needs both { and ;
  if (s.find('{') == std::string_view::npos ||
      s.find(';') == std::string_view::npos)
    return {Language::Unknown, 0.0f};

  static constexpr std::string_view props[] = {
      "color:",       "background:",  "margin:",       "padding:",
      "border:",      "display:",     "position:",     "font-size:",
      "font-family:", "width:",       "height:",       "top:",
      "left:",        "right:",       "bottom:",       "z-index:",
      "overflow:",    "text-align:",  "float:",        "opacity:",
      "transform:",   "transition:",  "animation:",    "flex:",
      "grid:",        "justify-content:", "align-items:",
  };
  int matches = 0;
  for (const auto prop : props) {
    if (FindCI(s, prop) != std::string_view::npos) matches++;
  }
  if (matches == 0) return {Language::Unknown, 0.0f};
  return {Language::CSS, std::min(0.6f + matches * 0.1f, 0.95f)};
}

/// Detect shell commands by pipes, redirections, and leading commands.
static ClassificationResult DetectShell(const std::string_view s) {
  if (s.starts_with("#!/"))
    return {Language::Shell, 0.95f};

  int signals = 0;

  // Pipe operators (single |, not ||)
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '|') {
      const bool doublePipe =
          (i > 0 && s[i - 1] == '|') || (i + 1 < s.size() && s[i + 1] == '|');
      if (!doublePipe) signals += 2;
    }
  }

  // Redirections
  if (s.find(">>") != std::string_view::npos) signals++;
  if (s.find("2>&1") != std::string_view::npos) signals += 2;
  if (s.find("2>/dev/null") != std::string_view::npos) signals += 2;

  // Leading commands
  const auto trimmed = TrimLeft(s);
  static constexpr std::string_view cmds[] = {
      "echo ",  "cat ",   "grep ",  "sed ",   "awk ",   "find ",
      "xargs ", "ls ",    "cd ",    "mv ",    "cp ",    "rm ",
      "mkdir ", "chmod ", "chown ", "tar ",   "curl ",  "wget ",
      "ssh ",   "scp ",   "git ",   "docker ","make ",  "cmake ",
      "pip ",   "npm ",   "apt ",   "yum ",   "brew ",  "sudo ",
  };
  for (const auto cmd : cmds) {
    if (trimmed.starts_with(cmd)) { signals += 2; break; }
  }

  // Shell variables: $VAR or ${VAR}
  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (s[i] == '$' &&
        (std::isalpha(static_cast<unsigned char>(s[i + 1])) ||
         s[i + 1] == '{' || s[i + 1] == '('))
      signals++;
  }

  if (signals < 2) return {Language::Unknown, 0.0f};
  return {Language::Shell, std::min(0.55f + signals * 0.08f, 0.95f)};
}

/// Detect metric names, trace categories, symbol names, and resource-like tokens.
static ClassificationResult DetectIdentifierLike(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 4 || trimmed.size() > 160)
    return {Language::Unknown, 0.0f};

  if (trimmed.find_first_of(" \t\n\r") != std::string_view::npos)
    return {Language::Unknown, 0.0f};

  int letters = 0;
  int digits = 0;
  int separators = 0;
  int camelTransitions = 0;
  int componentStarts = 0;
  bool hasDoubleColon = false;
  bool hasDot = false;
  bool hasSlash = false;

  char prev = '\0';
  bool prevWasSeparator = true;
  for (const char c : trimmed) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc)) {
      if (std::isalpha(uc)) letters++;
      if (std::isdigit(uc)) digits++;
      if (prevWasSeparator) componentStarts++;
      if (std::islower(static_cast<unsigned char>(prev)) &&
          std::isupper(uc))
        camelTransitions++;
      prevWasSeparator = false;
    } else if (IsIdentifierLikeSeparator(c)) {
      separators++;
      prevWasSeparator = true;
      if (c == '.') hasDot = true;
      if (c == '/') hasSlash = true;
      if (c == ':' && prev == ':') hasDoubleColon = true;
    } else {
      return {Language::Unknown, 0.0f};
    }
    prev = c;
  }

  if (letters == 0)
    return {Language::Unknown, 0.0f};
  if (separators == 0 && camelTransitions == 0)
    return {Language::Unknown, 0.0f};

  const float letterRatio =
      static_cast<float>(letters) / static_cast<float>(trimmed.size());
  if (letterRatio < 0.45f)
    return {Language::Unknown, 0.0f};

  const int signals = componentStarts + camelTransitions +
                      (hasDoubleColon ? 2 : 0) +
                      (hasDot ? 1 : 0) + (hasSlash ? 1 : 0);
  if (signals < 2)
    return {Language::Unknown, 0.0f};

  if (digits > 0 && letters < 3)
    return {Language::Unknown, 0.0f};

  return {Language::IdentifierLike,
          std::min(0.62f + static_cast<float>(signals) * 0.05f, 0.95f)};
}

/// Detect ordinary prose, log lines, and other natural-language fragments.
static ClassificationResult DetectPlainText(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 12) return {Language::Unknown, 0.0f};

  int letters = 0;
  int digits = 0;
  int spaces = 0;
  int words = 0;
  int sentenceMarks = 0;
  int codeSymbols = 0;
  bool inWord = false;

  for (const char c : trimmed) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalpha(uc)) letters++;
    if (std::isdigit(uc)) digits++;
    if (std::isspace(uc)) spaces++;

    const bool isWord =
        std::isalnum(uc) || c == '\'' || c == '-' || c == '_';
    if (isWord && !inWord) words++;
    inWord = isWord;

    if (c == '.' || c == '!' || c == '?' || c == ':') sentenceMarks++;
    if (c == '{' || c == '}' || c == '[' || c == ']' || c == '<' || c == '>' ||
        c == ';' || c == '=' || c == '$' || c == '|' || c == '&' || c == '@' ||
        c == '`')
      codeSymbols++;
  }

  if (trimmed.find("::") != std::string_view::npos ||
      trimmed.find("->") != std::string_view::npos ||
      trimmed.find("=>") != std::string_view::npos ||
      trimmed.find(":=") != std::string_view::npos ||
      trimmed.find("==") != std::string_view::npos)
    return {Language::Unknown, 0.0f};

  if (codeSymbols > 2) return {Language::Unknown, 0.0f};

  const int nonSpace = static_cast<int>(trimmed.size()) - spaces;
  if (nonSpace <= 0) return {Language::Unknown, 0.0f};

  const float letterRatio = static_cast<float>(letters) / static_cast<float>(nonSpace);
  const float digitRatio = static_cast<float>(digits) / static_cast<float>(nonSpace);

  if (letterRatio < 0.55f || digitRatio > 0.30f)
    return {Language::Unknown, 0.0f};

  if (words >= 4 && (spaces >= 2 || sentenceMarks > 0))
    return {Language::PlainText, std::min(0.70f + words * 0.03f, 0.92f)};

  if (words >= 6)
    return {Language::PlainText, std::min(0.68f + words * 0.025f, 0.90f)};

  return {Language::Unknown, 0.0f};
}

// ============================================================================
// Layer 2: Naive Bayes trigram classifier
// ============================================================================

/// Classify using the trained trigram language model.
///
/// Scoring uses the identity:
///   score(lang) = prior + N*unseen + sum(adjustedLogProb for found trigrams)
/// where adjustedLogProb = logProb - unseen, precomputed during training.
/// This avoids tracking which trigrams were NOT found per language.
static ClassificationResult NaiveBayesClassify(const std::string_view body) {
  if (kNumLanguages == 0 || kNumCombinedEntries == 0)
    return {Language::Unknown, 0.0f};

  const size_t numTrigrams = body.size() >= 3 ? body.size() - 2 : 0;
  if (numTrigrams == 0)
    return {Language::Unknown, 0.0f};

  // Initialize per-language scores: prior + N * unseen
  constexpr size_t kMaxLangs = 64;
  static_assert(kNumLanguages <= kMaxLangs, "Increase kMaxLangs");
  float scores[kMaxLangs];
  uint16_t matchedCounts[kMaxLangs]{};
  for (size_t i = 0; i < kNumLanguages; i++) {
    scores[i] = kLanguageInfos[i].logPrior +
                static_cast<float>(numTrigrams) * kLanguageInfos[i].unseenLogProb;
  }

  // For each trigram in the input, find matching entries in the combined table
  for (size_t i = 0; i + 2 < body.size(); i++) {
    const uint32_t tri = PackTrigram(body[i], body[i + 1], body[i + 2]);

    // Binary search for the first entry with this trigram
    size_t lo = 0, hi = kNumCombinedEntries;
    while (lo < hi) {
      const size_t mid = lo + (hi - lo) / 2;
      if (kCombinedEntries[mid].trigram < tri)
        lo = mid + 1;
      else
        hi = mid;
    }

    // Accumulate scores for all language entries with this trigram
    while (lo < kNumCombinedEntries && kCombinedEntries[lo].trigram == tri) {
      matchedCounts[kCombinedEntries[lo].languageIdx]++;
      scores[kCombinedEntries[lo].languageIdx] +=
          kCombinedEntries[lo].adjustedLogProb;
      lo++;
    }
  }

  // Find best and second-best scores
  float maxScore = -std::numeric_limits<float>::infinity();
  float secondMax = -std::numeric_limits<float>::infinity();
  size_t bestIdx = 0;
  for (size_t i = 0; i < kNumLanguages; i++) {
    if (scores[i] > maxScore) {
      secondMax = maxScore;
      maxScore = scores[i];
      bestIdx = i;
    } else if (scores[i] > secondMax) {
      secondMax = scores[i];
    }
  }

  // Softmax confidence: P(best | text) = 1 / sum(exp(score_i - maxScore))
  float sumExp = 0.0f;
  for (size_t i = 0; i < kNumLanguages; i++)
    sumExp += std::exp(scores[i] - maxScore);
  const float confidence = 1.0f / sumExp;

  const uint16_t bestMatched = matchedCounts[bestIdx];
  if (bestMatched < 6)
    return {Language::Unknown, 0.0f};

  const auto &modelTrigramCounts = ModelTrigramCounts();
  const float modelSize =
      static_cast<float>(std::max<uint16_t>(modelTrigramCounts[bestIdx], 1));
  const float matchRatio =
      static_cast<float>(bestMatched) / static_cast<float>(numTrigrams);
  const float requiredMatchRatio =
      std::min(0.80f, 0.18f + 7.0f / std::sqrt(modelSize));
  if (matchRatio < requiredMatchRatio)
    return {Language::Unknown, 0.0f};

  // Require meaningful separation: the per-trigram score gap between best and
  // second-best must exceed a threshold.  Without this, the classifier picks
  // whichever language model happens to score marginally higher on generic text.
  const float gap = (maxScore - secondMax) / static_cast<float>(numTrigrams);
  if (gap < 0.3f)
    return {Language::Unknown, 0.0f};

  return {static_cast<Language>(kLanguageInfos[bestIdx].languageId), confidence};
}

// ============================================================================
// Public API
// ============================================================================

ClassificationResult ClassifyString(const std::string_view body,
                                    const float minConfidence) {
  if (body.size() < 2)
    return {Language::Unknown, 0.0f};

  if (const auto result = DetectBinaryData(body); result.confidence >= minConfidence)
    return result;

  const std::string normalized = NormalizeForClassification(body);
  const std::string_view text = normalized.empty() ? body : std::string_view(normalized);

  // Layer 1: structural detectors (ordered by specificity, lowest FP first)
  using Detector = ClassificationResult (*)(std::string_view);
  static constexpr Detector detectors[] = {
      DetectURL,           DetectXML,          DetectFilePath,
      DetectHexData,       DetectSQL,          DetectJSON,
      DetectHTML,          DetectRegex,        DetectFormatString,
      DetectCSS,           DetectShell,        DetectYAML,
      DetectIdentifierLike,DetectPlainText,
  };

  for (const auto detect : detectors) {
    if (const auto result = detect(text); result.confidence >= minConfidence)
      return result;
  }

  // Layer 2: Naive Bayes trigram classifier (requires >= 15 chars).
  // With 30+ competing classes, softmax confidence must be high to be meaningful.
  if (text.size() >= 15) {
    const float nbThreshold = std::max(minConfidence, 0.85f);
    if (const auto result = NaiveBayesClassify(text);
        result.confidence >= nbThreshold)
      return result;
  }

  return {Language::Unknown, 0.0f};
}
