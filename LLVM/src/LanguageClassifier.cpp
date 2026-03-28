//
// LanguageClassifier.cpp
// Author: Antoine Bastide
// Date: 24/03/2026
//

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

#include "../include/LanguageClassifier.hpp"
#include "../include/LanguageModel.generated.hpp"

// ============================================================================
// Language name table
// ============================================================================

const char *LanguageName(const Language lang) {
  static constexpr const char *names[] = {
    "Unknown", "PlainText", "URL", "FilePath", "FormatString",
    "SQL", "HTML", "XML", "JSON", "YAML",
    "CSS", "Regex", "Shell", "Python", "JavaScript",
    "TypeScript", "Java", "C", "C++", "C#",
    "Go", "Rust", "Ruby", "PHP", "Perl",
    "Lua", "Swift", "Kotlin", "R", "Scala",
    "Haskell", "OCaml", "Erlang", "Elixir", "Dart",
    "Objective-C", "GLSL", "HLSL", "IdentifierLike",
    "HexData", "BinaryData", "InlineAsm", "Email",
    "PseudoURL", "PseudoEmail",
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
  if (s.size() < prefix.size())
    return false;
  for (size_t i = 0; i < prefix.size(); i++) {
    if (ToLower(s[i]) != ToLower(prefix[i]))
      return false;
  }
  return true;
}

/// Case-insensitive substring search. Returns position or npos.
static size_t FindCI(const std::string_view haystack, const std::string_view needle) {
  if (needle.empty())
    return 0;
  if (needle.size() > haystack.size())
    return std::string_view::npos;
  const size_t last = haystack.size() - needle.size();
  for (size_t i = 0; i <= last; i++) {
    bool found = true;
    for (size_t j = 0; j < needle.size(); j++) {
      if (ToLower(haystack[i + j]) != ToLower(needle[j])) {
        found = false;
        break;
      }
    }
    if (found)
      return i;
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

    if (line.starts_with("/*"))
      line = TrimLeft(line.substr(2));
    if (line.ends_with("*/"))
      line = TrimRight(line.substr(0, line.size() - 2));
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
      if (wroteLine)
        cleaned.push_back('\n');
      cleaned.append(line);
      wroteLine = true;
    }

    if (end == std::string_view::npos)
      break;
    pos = end + 1;
  }

  if (!cleaned.empty())
    return cleaned;
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

    switch (body[i + 1]) {
      case '\\':
        result.push_back('\\');
        i++;
        break;
      case '"':
        result.push_back('"');
        i++;
        break;
      case '\'':
        result.push_back('\'');
        i++;
        break;
      case 'n':
        result.push_back('\n');
        i++;
        break;
      case 'r':
        result.push_back('\r');
        i++;
        break;
      case 't':
        result.push_back('\t');
        i++;
        break;
      default:
        result.push_back(body[i]);
        break;
    }
  }

  return result;
}

static std::string NormalizeForClassification(const std::string_view body) {
  return UnescapeCommonSequences(StripCommentDecorators(body));
}

static bool IsSimpleKey(const std::string_view s) {
  if (s.empty())
    return false;
  return std::ranges::all_of(
    s, [](const unsigned char c) {
      return std::isalnum(c) || c == '_' || c == '-' || c == '.';
    }
  );
}

struct YAMLAnalysis {
  int docMarkers = 0;
  int keyValueLines = 0;
  int blockKeyLines = 0;
  int listLines = 0;
  int nonEmptyLines = 0;
};

static bool LooksLikeYAMLKey(const std::string_view key) {
  const auto trimmed = Trim(key);
  if (trimmed.empty() || trimmed.size() > 32 || !IsSimpleKey(trimmed))
    return false;

  bool hasAlpha = false;
  bool hasLower = false;
  bool allDigits = true;
  for (const char c: trimmed) {
    if (const auto uc = static_cast<unsigned char>(c); std::isalpha(uc)) {
      hasAlpha = true;
      allDigits = false;
      if (std::islower(uc))
        hasLower = true;
    } else if (!std::isdigit(uc)) {
      allDigits = false;
    }
  }

  if (!hasAlpha || allDigits)
    return false;
  if (std::isdigit(static_cast<unsigned char>(trimmed.front())))
    return false;
  if (!hasLower && trimmed.size() <= 4)
    return false;
  return true;
}

static bool LooksLikeYAMLScalarValue(const std::string_view value) {
  const auto trimmed = Trim(value);
  if (trimmed.empty())
    return false;
  if (trimmed.size() > 80)
    return false;
  if (trimmed.find('\t') != std::string_view::npos ||
      trimmed.find('{') != std::string_view::npos ||
      trimmed.find('}') != std::string_view::npos ||
      trimmed.find(';') != std::string_view::npos)
    return false;

  int words = 0;
  bool inWord = false;
  for (const char c: trimmed) {
    const auto uc = static_cast<unsigned char>(c);
    const bool isWord = std::isalnum(uc) || c == '_' || c == '-' || c == '.' ||
                        c == '/' || c == '"' || c == '\'' || c == '@';
    if (isWord && !inWord)
      words++;
    inWord = isWord;
  }

  if (words > 12)
    return false;
  if (trimmed.find(". ") != std::string_view::npos ||
      trimmed.find("? ") != std::string_view::npos ||
      trimmed.find("! ") != std::string_view::npos)
    return false;
  return true;
}

static bool LooksLikeYAMLListItem(const std::string_view item) {
  const auto trimmed = Trim(item);
  if (trimmed.empty() || trimmed.find('\t') != std::string_view::npos)
    return false;

  const size_t colon = trimmed.find(':');
  if (colon != std::string_view::npos && colon > 0 && colon + 1 < trimmed.size()) {
    const auto key = TrimRight(trimmed.substr(0, colon));
    const auto value = TrimLeft(trimmed.substr(colon + 1));
    return LooksLikeYAMLKey(key) && LooksLikeYAMLScalarValue(value);
  }

  int words = 0;
  bool inWord = false;
  for (const char c: trimmed) {
    const auto uc = static_cast<unsigned char>(c);
    const bool isWord = std::isalnum(uc) || c == '_' || c == '-' || c == '.';
    if (isWord && !inWord)
      words++;
    inWord = isWord;
  }

  if (words == 0 || words > 4)
    return false;
  if (trimmed.find(". ") != std::string_view::npos ||
      trimmed.find("? ") != std::string_view::npos ||
      trimmed.find("! ") != std::string_view::npos)
    return false;
  return true;
}

static YAMLAnalysis AnalyzeYAMLStructure(const std::string_view s) {
  YAMLAnalysis analysis;

  size_t pos = 0;
  while (pos <= s.size()) {
    const size_t end = s.find('\n', pos);
    const size_t lineEnd = end == std::string_view::npos ? s.size() : end;
    const auto line = Trim(s.substr(pos, lineEnd - pos));

    if (!line.empty()) {
      analysis.nonEmptyLines++;
      if (line == "---" || line == "...") {
        analysis.docMarkers++;
      } else if (line.starts_with("- ")) {
        if (LooksLikeYAMLListItem(line.substr(2)))
          analysis.listLines++;
      } else if (line.find('\t') == std::string_view::npos) {
        if (const size_t colon = line.find(':'); colon != std::string_view::npos && colon > 0) {
          const auto key = TrimRight(line.substr(0, colon));
          const auto value = colon + 1 < line.size()
                               ? TrimLeft(line.substr(colon + 1))
                               : std::string_view{};
          if (LooksLikeYAMLKey(key)) {
            if (value.empty())
              analysis.blockKeyLines++;
            else if (LooksLikeYAMLScalarValue(value))
              analysis.keyValueLines++;
          }
        }
      }
    }

    if (end == std::string_view::npos)
      break;
    pos = end + 1;
  }

  return analysis;
}

static bool HasStrongYAMLEvidence(const std::string_view s) {
  const auto [docMarkers, keyValueLines, blockKeyLines, listLines, nonEmptyLines] = AnalyzeYAMLStructure(s);
  if (nonEmptyLines == 0)
    return false;

  if (docMarkers > 0 && keyValueLines + blockKeyLines + listLines >= 2)
    return true;

  if (blockKeyLines > 0 && (listLines > 0 || keyValueLines > 0))
    return true;

  return false;
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

static bool IsLabelPunctuation(const char c) {
  return c == '.' || c == ',' || c == ':' || c == ';' || c == '!' ||
         c == '?' || c == '-' || c == '(' || c == ')';
}

static bool IsCodeKeywordToken(const std::string_view token) {
  if (token.empty() || token.size() > 16)
    return false;

  char lower[17];
  for (size_t i = 0; i < token.size(); i++)
    lower[i] = ToLower(token[i]);
  const std::string_view lowerToken(lower, token.size());

  static constexpr std::string_view keywords[] = {
    "async", "await", "bool", "break", "case", "catch",
    "char", "class", "const", "continue", "def", "default",
    "do", "double", "else", "enum", "false", "float",
    "fn", "for", "function", "if", "import", "int",
    "interface", "let", "namespace", "new", "null", "package",
    "private", "protected", "public", "return", "static", "struct",
    "switch", "template", "true", "try", "typename", "using",
    "var", "void", "while",
  };
  return std::ranges::binary_search(keywords, lowerToken);
}

static bool LooksLikeShortPlainTextLabel(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 8 || trimmed.size() > 80)
    return false;

  int words = 0;
  int alphaWords = 0;
  int keywordWords = 0;
  int letters = 0;
  int digits = 0;
  int spaces = 0;

  for (const char c: trimmed) {
    if (std::isspace(static_cast<unsigned char>(c)))
      spaces++;
  }

  size_t pos = 0;
  while (pos <= trimmed.size()) {
    const size_t end = trimmed.find_first_of(" \t\n\r", pos);
    std::string_view token = trimmed.substr(
      pos, end == std::string_view::npos ? trimmed.size() - pos : end - pos
    );

    while (!token.empty() && IsLabelPunctuation(token.front()))
      token.remove_prefix(1);
    while (!token.empty() && IsLabelPunctuation(token.back()))
      token.remove_suffix(1);

    if (!token.empty()) {
      int tokenLetters = 0;
      int tokenDigits = 0;
      for (const char c: token) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalpha(uc)) {
          tokenLetters++;
          letters++;
        } else if (std::isdigit(uc)) {
          tokenDigits++;
          digits++;
        } else if (c != '\'' && c != '-') {
          return false;
        }
      }

      if (tokenLetters == 0)
        return false;

      words++;
      if (tokenLetters >= tokenDigits)
        alphaWords++;
      if (IsCodeKeywordToken(token))
        keywordWords++;
    }

    if (end == std::string_view::npos)
      break;
    pos = end + 1;
  }

  if (words < 2 || words > 5)
    return false;
  if (alphaWords < 2)
    return false;
  if (keywordWords == words)
    return false;
  if (digits > letters / 2)
    return false;

  const int nonSpace = static_cast<int>(trimmed.size()) - spaces;
  if (nonSpace <= 0)
    return false;

  const float letterRatio =
      static_cast<float>(letters) / static_cast<float>(nonSpace);
  return letterRatio >= 0.65f;
}

static bool HasRepeatedNonLetterRun(const std::string_view s, int *maxRunLength = nullptr) {
  int repeatedRunLength = 1;
  int maxRepeatedRun = 1;
  char repeatedChar = '\0';
  bool hasRepeatedNonLetterRun = false;

  for (const char c: s) {
    const auto uc = static_cast<unsigned char>(c);
    if (!std::isalpha(uc) && !std::isspace(uc)) {
      if (c == repeatedChar) {
        repeatedRunLength++;
      } else {
        repeatedChar = c;
        repeatedRunLength = 1;
      }
      maxRepeatedRun = std::max(maxRepeatedRun, repeatedRunLength);
      if (repeatedRunLength >= 5)
        hasRepeatedNonLetterRun = true;
    } else {
      repeatedChar = '\0';
      repeatedRunLength = 1;
    }
  }

  if (maxRunLength != nullptr)
    *maxRunLength = maxRepeatedRun;
  return hasRepeatedNonLetterRun;
}

static bool IsHumanWordToken(const std::string_view token) {
  if (token.empty())
    return false;

  int letters = 0;
  int uppercase = 0;
  int lowercase = 0;
  for (const char c: token) {
    const auto uc = static_cast<unsigned char>(c);
    if (!std::isalpha(uc))
      return false;
    letters++;
    if (std::isupper(uc))
      uppercase++;
    else
      lowercase++;
  }

  if (letters < 3)
    return false;
  if (uppercase == letters || lowercase == letters)
    return true;
  return std::isupper(static_cast<unsigned char>(token.front())) &&
         lowercase >= 2 && uppercase <= 3;
}

static std::string ExtractDecoratedTextCore(const std::string_view s) {
  std::string core;
  size_t pos = 0;

  while (pos <= s.size()) {
    const size_t lineEnd = s.find('\n', pos);
    auto line = Trim(
      s.substr(
        pos, lineEnd == std::string_view::npos
               ? s.size() - pos
               : lineEnd - pos
      )
    );

    while (!line.empty() &&
           !std::isalnum(static_cast<unsigned char>(line.front())))
      line.remove_prefix(1);
    while (!line.empty() &&
           !std::isalnum(static_cast<unsigned char>(line.back())))
      line.remove_suffix(1);

    if (!line.empty()) {
      if (!core.empty())
        core.push_back(' ');
      core.append(line);
    }

    if (lineEnd == std::string_view::npos)
      break;
    pos = lineEnd + 1;
  }

  return core;
}

static bool LooksLikeDecoratedPlainText(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 8)
    return false;

  if (!HasRepeatedNonLetterRun(trimmed))
    return false;

  const std::string core = ExtractDecoratedTextCore(trimmed);
  const auto coreView = Trim(std::string_view(core));
  if (coreView.empty())
    return false;

  if (coreView.find("::") != std::string_view::npos ||
      coreView.find("->") != std::string_view::npos ||
      coreView.find("=>") != std::string_view::npos ||
      coreView.find(":=") != std::string_view::npos ||
      coreView.find("==") != std::string_view::npos)
    return false;

  if (LooksLikeShortPlainTextLabel(coreView))
    return true;

  int words = 0;
  int alphaWords = 0;
  int letters = 0;
  int digits = 0;
  int codeSymbols = 0;
  size_t pos = 0;

  while (pos <= coreView.size()) {
    const size_t end = coreView.find_first_of(" \t\n\r", pos);
    std::string_view token = coreView.substr(
      pos, end == std::string_view::npos ? coreView.size() - pos : end - pos
    );
    if (!token.empty()) {
      words++;
      int tokenLetters = 0;
      bool lettersOnly = true;
      for (const char c: token) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalpha(uc)) {
          tokenLetters++;
          letters++;
        } else if (std::isdigit(uc)) {
          digits++;
          lettersOnly = false;
        } else if (c == '\'' || c == '-') {
          lettersOnly = false;
        } else {
          lettersOnly = false;
          if (c == '{' || c == '}' || c == '[' || c == ']' || c == '<' ||
              c == '>' || c == ';' || c == '=' || c == '$' || c == '|' ||
              c == '&' || c == '@' || c == '`' || c == '_' || c == '/' ||
              c == '\\')
            codeSymbols++;
        }
      }

      if (tokenLetters >= 2)
        alphaWords++;
      if (words == 1 && lettersOnly && IsHumanWordToken(token))
        return true;
    }

    if (end == std::string_view::npos)
      break;
    pos = end + 1;
  }

  if (codeSymbols > 2)
    return false;
  if (alphaWords == 0)
    return false;
  if (digits > letters / 2)
    return false;
  return words >= 2 && alphaWords >= 1;
}

static ClassificationResult DetectSeparatorLine(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 8)
    return {Language::Unknown, 0.0f};

  int letters = 0;
  int digits = 0;
  int maxRepeatedRun = 1;
  int shortAlphaRun = 0;
  int maxAlphaRun = 0;

  for (const char c: trimmed) {
    const auto uc = static_cast<unsigned char>(c);
    if (std::isalpha(uc))
      letters++;
    else if (std::isdigit(uc))
      digits++;
    if (std::isalpha(uc)) {
      shortAlphaRun++;
      maxAlphaRun = std::max(maxAlphaRun, shortAlphaRun);
    } else {
      shortAlphaRun = 0;
    }
  }

  if (!HasRepeatedNonLetterRun(trimmed, &maxRepeatedRun))
    return {Language::Unknown, 0.0f};

  if (letters > 0 || digits > 0)
    return (digits == 0 && letters <= 2 && maxAlphaRun <= 1)
             ? ClassificationResult{Language::Unknown, 1.0f}
             : ClassificationResult{Language::Unknown, 0.0f};

  if (maxRepeatedRun < 5)
    return {Language::Unknown, 0.0f};

  return {Language::Unknown, 1.0f};
}

/// Check whether a tag name is a known HTML5 element (case-insensitive).
static bool IsHTMLTagName(const std::string_view name) {
  if (name.empty() || name.size() > 10)
    return false;

  // Lowercase the tag name into a small stack buffer.
  char lower[11];
  for (size_t i = 0; i < name.size(); i++)
    lower[i] = ToLower(name[i]);
  const std::string_view lName(lower, name.size());

  // Sorted list of common HTML5 element names.
  static constexpr std::string_view tags[] = {
    "a", "abbr", "address", "article", "aside", "b",
    "body", "br", "button", "canvas", "caption", "code",
    "col", "dd", "details", "div", "dl", "dt",
    "em", "fieldset", "figure", "footer", "form", "h1",
    "h2", "h3", "h4", "h5", "h6", "head",
    "header", "hr", "html", "i", "iframe", "img",
    "input", "label", "li", "link", "main", "meta",
    "nav", "ol", "option", "p", "pre", "script",
    "section", "select", "small", "span", "strong", "style",
    "summary", "svg", "table", "tbody", "td", "template",
    "textarea", "tfoot", "th", "thead", "title", "tr",
    "u", "ul", "video",
  };
  return std::ranges::binary_search(tags, lName);
}

static bool IsXMLNameStartChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalpha(uc) || c == '_';
}

static bool IsXMLNameChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '_' || c == '-' || c == '.';
}

static bool LooksLikeXMLQualifiedName(const std::string_view name) {
  if (name.empty() || name.find("::") != std::string_view::npos)
    return false;

  const size_t colon = name.find(':');
  if (colon == std::string_view::npos || colon == 0 || colon + 1 >= name.size())
    return false;
  if (name.find(':', colon + 1) != std::string_view::npos)
    return false;

  const auto prefix = name.substr(0, colon);
  const auto local = name.substr(colon + 1);
  if (!IsXMLNameStartChar(prefix.front()) || !IsXMLNameStartChar(local.front()))
    return false;

  return std::ranges::all_of(prefix.substr(1), IsXMLNameChar) &&
         std::ranges::all_of(local.substr(1), IsXMLNameChar);
}

static bool IsURLChar(const char c) {
  if (std::isalnum(static_cast<unsigned char>(c)))
    return true;
  switch (c) {
    case '-':
    case '.':
    case '_':
    case '~':
    case ':':
    case '/':
    case '?':
    case '#':
    case '[':
    case ']':
    case '@':
    case '!':
    case '$':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '*':
    case '+':
    case ',':
    case ';':
    case '=':
    case '%':
      return true;
    default:
      return false;
  }
}

struct TokenMatch {
  size_t start = std::string_view::npos;
  size_t end = std::string_view::npos;
  int count = 0;
};

static std::string_view StripURLWrappers(const std::string_view s) {
  auto trimmed = Trim(s);
  auto isWrapper = [](const char c) {
    switch (c) {
      case '<':
      case '>':
      case '(':
      case ')':
      case '[':
      case ']':
      case '{':
      case '}':
      case '"':
      case '\'':
      case '`':
      case '.':
      case ':':
      case '!':
      case '?':
      case ',':
      case ';':
        return true;
      default:
        return false;
    }
  };

  while (!trimmed.empty() && isWrapper(trimmed.front()))
    trimmed.remove_prefix(1);
  while (!trimmed.empty() && isWrapper(trimmed.back()))
    trimmed.remove_suffix(1);
  return Trim(trimmed);
}

static bool IsLikelyDataURL(const std::string_view s) {
  if (!StartsWithCI(s, "data:"))
    return false;

  const auto rest = s.substr(5);
  const size_t comma = rest.find(',');
  if (comma == std::string_view::npos)
    return false;

  for (size_t i = 0; i < comma; i++) {
    const char c = rest[i];
    if (std::isspace(static_cast<unsigned char>(c)))
      return false;
    if (std::isalnum(static_cast<unsigned char>(c)))
      continue;
    switch (c) {
      case '!':
      case '$':
      case '&':
      case '\'':
      case '(':
      case ')':
      case '*':
      case '+':
      case '-':
      case '.':
      case '/':
      case ':':
      case ';':
      case '=':
      case '?':
      case '@':
      case '_':
      case '~':
      case '%':
        continue;
      default:
        return false;
    }
  }

  return true;
}

static bool LooksLikeBriefURLContext(const std::string_view s) {
  const auto trimmed = StripURLWrappers(s);
  if (trimmed.empty())
    return true;

  if (LooksLikeShortPlainTextLabel(trimmed))
    return true;

  int words = 0;
  int letters = 0;
  int digits = 0;
  int other = 0;
  bool inWord = false;
  for (const char c: trimmed) {
    const auto uc = static_cast<unsigned char>(c);
    const bool isWord = std::isalnum(uc) || c == '\'' || c == '-' || c == '.';
    if (isWord && !inWord)
      words++;
    inWord = isWord;

    if (std::isalpha(uc))
      letters++;
    else if (std::isdigit(uc))
      digits++;
    else if (!std::isspace(uc) && c != ':' && c != '.')
      other++;
  }

  if (words == 0 || words > 4)
    return false;
  if (letters < 3 || digits > letters / 2)
    return false;
  return other == 0;
}

static TokenMatch FindSingleURLToken(const std::string_view s) {
  static constexpr std::string_view schemes[] = {
    "http://", "https://", "ftp://", "ftps://", "file://",
    "mailto:", "ssh://", "git://", "svn://", "telnet://",
    "ws://", "wss://", "data:",
  };

  TokenMatch match;
  for (const auto scheme: schemes) {
    size_t searchPos = 0;
    while (searchPos < s.size()) {
      const size_t rel = FindCI(s.substr(searchPos), scheme);
      if (rel == std::string_view::npos)
        break;

      const size_t pos = searchPos + rel;
      if (pos > 0 && IsURLChar(s[pos - 1])) {
        searchPos = pos + 1;
        continue;
      }

      if (scheme == "data:" && !IsLikelyDataURL(s.substr(pos))) {
        searchPos = pos + 1;
        continue;
      }

      size_t end = pos + scheme.size();
      while (end < s.size() && IsURLChar(s[end]))
        end++;
      while (end > pos + scheme.size() &&
             (s[end - 1] == '.' || s[end - 1] == ',' || s[end - 1] == ';' ||
              s[end - 1] == ':' || s[end - 1] == '!' || s[end - 1] == '?'))
        end--;

      if (end <= pos + scheme.size()) {
        searchPos = pos + 1;
        continue;
      }

      match.count++;
      if (match.count > 1)
        return match;

      match.start = pos;
      match.end = end;
      searchPos = end;
    }
  }

  return match;
}

static bool IsEmailLocalChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-';
}

static bool IsEmailDomainChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '.' || c == '-';
}

static bool IsLikelyEmailToken(const std::string_view token) {
  const size_t at = token.find('@');
  if (at == std::string_view::npos || at == 0 || at + 1 >= token.size())
    return false;
  if (token.find('@', at + 1) != std::string_view::npos)
    return false;

  const auto local = token.substr(0, at);
  const auto domain = token.substr(at + 1);
  if (local.empty() || domain.empty())
    return false;

  if (!std::ranges::all_of(local, [](const char c) { return IsEmailLocalChar(c); }))
    return false;

  if (local.front() == '.' || local.back() == '.')
    return false;
  if (local.find("..") != std::string_view::npos)
    return false;

  if (!std::ranges::all_of(domain, [](const char c) { return IsEmailDomainChar(c); }))
    return false;
  if (domain.front() == '.' || domain.back() == '.')
    return false;
  if (domain.find('.') == std::string_view::npos)
    return false;

  std::string_view finalLabel;
  size_t pos = 0;
  while (pos <= domain.size()) {
    const size_t dot = domain.find('.', pos);
    const auto label = domain.substr(
      pos, dot == std::string_view::npos ? domain.size() - pos : dot - pos
    );
    if (label.empty() || label.front() == '-' || label.back() == '-')
      return false;

    for (const char c: label) {
      const auto uc = static_cast<unsigned char>(c);
      if (!std::isalnum(uc) && c != '-')
        return false;
    }

    finalLabel = label;
    if (dot == std::string_view::npos)
      break;
    pos = dot + 1;
  }

  if (finalLabel.size() < 2 || finalLabel.size() > 24)
    return false;

  if (StartsWithCI(finalLabel, "xn--")) {
    if (finalLabel.size() < 6)
      return false;
    return std::ranges::all_of(finalLabel.substr(4), [](const char c) {
      const auto uc = static_cast<unsigned char>(c);
      return std::isalnum(uc) || c == '-';
    });
  }

  return std::ranges::all_of(finalLabel, [](const char c) {
    return std::isalpha(static_cast<unsigned char>(c));
  });
}

static bool LooksLikeBareDomainLikeToken(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.empty() || trimmed.find('@') != std::string_view::npos ||
      trimmed.find_first_of(" \t\n\r/:<>") != std::string_view::npos)
    return false;

  if (trimmed.starts_with(".") || trimmed.ends_with(".") ||
      trimmed.find("..") != std::string_view::npos)
    return false;

  if (const size_t firstDot = trimmed.find('.'); firstDot == std::string_view::npos)
    return false;

  const size_t lastDot = trimmed.rfind('.');
  const auto tld = trimmed.substr(lastDot + 1);
  if (const int dotCount = static_cast<int>(std::ranges::count(trimmed, '.')); dotCount < 2 &&
                                                                               !(tld.size() >= 2 && tld.size() <= 4))
    return false;

  bool hasAlpha = false;
  size_t pos = 0;
  while (pos <= trimmed.size()) {
    const size_t dot = trimmed.find('.', pos);
    const auto label = trimmed.substr(
      pos, dot == std::string_view::npos ? trimmed.size() - pos : dot - pos
    );
    if (label.empty() || label.front() == '-' || label.back() == '-')
      return false;
    for (const char c: label) {
      if (const auto uc = static_cast<unsigned char>(c); std::isalpha(uc))
        hasAlpha = true;
      else if (!std::isdigit(uc) && c != '-')
        return false;
    }
    if (dot == std::string_view::npos)
      break;
    pos = dot + 1;
  }

  return hasAlpha;
}

static TokenMatch FindSingleEmailToken(const std::string_view s) {
  TokenMatch match;

  size_t pos = 0;
  while (pos < s.size()) {
    const size_t at = s.find('@', pos);
    if (at == std::string_view::npos)
      break;

    size_t start = at;
    while (start > 0 && IsEmailLocalChar(s[start - 1]))
      start--;

    if (at == start || at + 1 >= s.size()) {
      pos = at + 1;
      continue;
    }

    size_t end = at + 1;
    while (end < s.size() && IsEmailDomainChar(s[end]))
      end++;
    while (end > at + 1 &&
           (s[end - 1] == '.' || s[end - 1] == ',' || s[end - 1] == ';' ||
            s[end - 1] == ':' || s[end - 1] == '!' || s[end - 1] == '?'))
      end--;

    if (const auto token = s.substr(start, end - start); !IsLikelyEmailToken(token)) {
      pos = at + 1;
      continue;
    }

    match.count++;
    if (match.count > 1)
      return match;

    match.start = start;
    match.end = end;
    pos = end;
  }

  return match;
}

static bool HasTightPunctuationContinuation(const std::string_view side) {
  if (side.empty())
    return false;

  auto IsTerminalPunctuation = [](const char c) {
    return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?';
  };

  size_t pos = 0;
  while (pos < side.size() && IsTerminalPunctuation(side[pos]))
    pos++;

  if (pos == 0 || pos >= side.size())
    return false;

  const auto next = static_cast<unsigned char>(side[pos]);
  if (std::isspace(next))
    return false;
  if (side[pos] == ')' || side[pos] == ']' || side[pos] == '}' ||
      side[pos] == '>' || side[pos] == '"' || side[pos] == '\'')
    return false;

  return true;
}

static bool IsOnlyTerminalPunctuation(const std::string_view s) {
  if (s.empty())
    return false;
  return std::ranges::all_of(s, [](const char c) {
    return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?';
  });
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
    for (const auto kCombinedEntry: kCombinedEntries) {
      if (kCombinedEntry.languageIdx < values.size())
        values[kCombinedEntry.languageIdx]++;
    }
    return values;
  }();
  return counts;
}

// ============================================================================
// Layer 1: Structural detectors
// ============================================================================

/// Detect URLs and URL-containing wrapper strings.
static ClassificationResult DetectURL(const std::string_view s) {
  const auto match = FindSingleURLToken(s);
  if (match.count != 1)
    return {Language::Unknown, 0.0f};

  const auto prefix = StripURLWrappers(s.substr(0, match.start));
  const auto suffix = StripURLWrappers(s.substr(match.end));
  if (!prefix.empty() && !suffix.empty() &&
      (!LooksLikeBriefURLContext(prefix) || !LooksLikeBriefURLContext(suffix)))
    return {Language::Unknown, 0.0f};

  if (!prefix.empty() && !LooksLikeBriefURLContext(prefix))
    return {Language::Unknown, 0.0f};
  if (!suffix.empty() && !LooksLikeBriefURLContext(suffix))
    return {Language::Unknown, 0.0f};

  if (prefix.empty() && suffix.empty())
    return {Language::URL, 0.95f};
  return {Language::PseudoURL, 0.88f};
}

/// Detect email addresses and email-containing wrapper strings.
static ClassificationResult DetectEmail(const std::string_view s) {
  const auto match = FindSingleEmailToken(s);
  if (match.count != 1)
    return {Language::Unknown, 0.0f};

  const auto rawPrefix = s.substr(0, match.start);
  const auto rawSuffix = s.substr(match.end);
  if (HasTightPunctuationContinuation(rawPrefix) || HasTightPunctuationContinuation(rawSuffix))
    return {Language::Unknown, 0.0f};
  if (rawPrefix.empty() && IsOnlyTerminalPunctuation(rawSuffix))
    return {Language::Unknown, 0.0f};
  if (rawSuffix.empty() && IsOnlyTerminalPunctuation(rawPrefix))
    return {Language::Unknown, 0.0f};

  const auto prefix = StripURLWrappers(rawPrefix);
  const auto suffix = StripURLWrappers(rawSuffix);
  if (!prefix.empty() && !suffix.empty() &&
      (!LooksLikeBriefURLContext(prefix) || !LooksLikeBriefURLContext(suffix)))
    return {Language::Unknown, 0.0f};

  if (!prefix.empty() && !LooksLikeBriefURLContext(prefix))
    return {Language::Unknown, 0.0f};
  if (!suffix.empty() && !LooksLikeBriefURLContext(suffix))
    return {Language::Unknown, 0.0f};

  if (prefix.empty() && suffix.empty())
    return {Language::Email, 0.95f};
  return {Language::PseudoEmail, 0.88f};
}

/// Detect file paths by prefix patterns and separator density.
static ClassificationResult DetectFilePath(const std::string_view s) {
  if (s.size() < 2)
    return {Language::Unknown, 0.0f};

  // Windows absolute: C:\ or C:/
  if (s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) &&
      s[1] == ':' && (s[2] == '\\' || s[2] == '/'))
    return {Language::FilePath, 0.85f};

  // Unix absolute
  if (s[0] == '/' && s[1] != '*' && s[1] != '/') {
    int slashes = 0;
    for (const char c: s)
      if (c == '/')
        slashes++;
    if (slashes >= 2)
      return {Language::FilePath, 0.80f};
    return {Language::FilePath, 0.55f};
  }

  // Relative: ./ or ../
  if (s.starts_with("./") || s.starts_with("../"))
    return {Language::FilePath, 0.75f};

  return {Language::Unknown, 0.0f};
}

/// Detect printf-style format strings by counting specifiers.
static ClassificationResult DetectFormatString(const std::string_view s) {
  auto isFlag = [](const char c) {
    return c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' || c == '\'';
  };
  auto isConversion = [](const char c) {
    switch (c) {
      case 'd':
      case 'i':
      case 'u':
      case 'o':
      case 'x':
      case 'X':
      case 'f':
      case 'F':
      case 'e':
      case 'E':
      case 'g':
      case 'G':
      case 'a':
      case 'A':
      case 'c':
      case 's':
      case 'p':
      case 'n':
      case 'm':
        return true;
      default:
        return false;
    }
  };

  int specs = 0;
  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (s[i] != '%')
      continue;
    if (s[i + 1] == '%') {
      i++;
      continue;
    }

    size_t j = i + 1;

    const size_t positionalStart = j;
    while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
      j++;
    if (j < s.size() && s[j] == '$' && j > positionalStart) {
      j++;
    } else {
      j = i + 1;
    }

    while (j < s.size() && isFlag(s[j]))
      j++;

    if (j < s.size() && s[j] == '*') {
      j++;
    } else {
      while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
        j++;
    }

    if (j < s.size() && s[j] == '.') {
      j++;
      if (j < s.size() && s[j] == '*') {
        j++;
      } else {
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
          j++;
      }
    }

    if (j + 1 < s.size()) {
      if ((s[j] == 'h' && s[j + 1] == 'h') || (s[j] == 'l' && s[j + 1] == 'l')) {
        j += 2;
      } else if (s[j] == 'h' || s[j] == 'l' || s[j] == 'j' || s[j] == 'z' ||
                 s[j] == 't' || s[j] == 'L') {
        j++;
      }
    } else if (j < s.size() &&
               (s[j] == 'h' || s[j] == 'l' || s[j] == 'j' || s[j] == 'z' ||
                s[j] == 't' || s[j] == 'L')) {
      j++;
    }

    if (j < s.size() && isConversion(s[j])) {
      specs++;
      i = j;
    }
  }
  if (specs == 0)
    return {Language::Unknown, 0.0f};
  const float density = static_cast<float>(specs) / static_cast<float>(s.size());
  return {Language::FormatString, std::min(0.5f + density * 10.0f, 0.95f)};
}

/// Detect GCC/Clang inline-assembly templates and assembler directives.
static ClassificationResult DetectInlineAsm(const std::string_view s) {
  if (s.size() < 4)
    return {Language::Unknown, 0.0f};

  int signals = 0;

  static constexpr std::string_view directives[] = {
    ".align", ".ascii", ".asciz", ".balign", ".byte", ".fill",
    ".globl", ".inst", ".long", ".macro", ".octa", ".popsection",
    ".previous", ".pushsection", ".quad", ".section", ".short",
    ".size", ".type", ".word",
  };
  for (const auto directive: directives) {
    if (FindCI(s, directive) != std::string_view::npos)
      signals += 2;
  }

  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (s[i] != '%')
      continue;

    if (s[i + 1] == '[') {
      const size_t close = s.find(']', i + 2);
      if (close != std::string_view::npos && close > i + 2)
        signals += 2;
      continue;
    }

    if (!std::isdigit(static_cast<unsigned char>(s[i + 1])))
      continue;

    size_t j = i + 1;
    while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
      j++;
    if (j >= s.size()) {
      signals += 2;
      continue;
    }

    const char next = s[j];
    if (next == ',' || std::isspace(static_cast<unsigned char>(next)) ||
        next == ')' || next == '(' || next == ']' || next == '"' ||
        next == ';' || next == ':') {
      signals += 2;
    }
  }

  if (FindCI(s, "\\n.") != std::string_view::npos ||
      FindCI(s, "\n.") != std::string_view::npos)
    signals++;

  if (signals < 2)
    return {Language::Unknown, 0.0f};

  return {
    Language::InlineAsm,
    std::min(0.70f + static_cast<float>(signals) * 0.05f, 0.97f)
  };
}

/// Detect escaped-byte blobs or strings containing many non-printable bytes.
static ClassificationResult DetectBinaryData(const std::string_view s) {
  if (s.size() < 8)
    return {Language::Unknown, 0.0f};

  int controlBytes = 0;
  int escapedBytes = 0;
  int hexEscapes = 0;
  int octalEscapes = 0;
  int nullEscapes = 0;
  int letters = 0;
  int spaces = 0;

  for (size_t i = 0; i < s.size(); i++) {
    const auto uc = static_cast<unsigned char>(s[i]);
    if (std::isalpha(uc))
      letters++;
    if (std::isspace(uc))
      spaces++;
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
      if (digits > 1)
        octalEscapes++;
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
    return {
      Language::BinaryData,
      std::min(0.76f + static_cast<float>(strongEscapes) * 0.03f, 0.97f)
    };

  if (hexEscapes >= 3 && escapeRatio >= 0.40f)
    return {
      Language::BinaryData,
      std::min(0.74f + static_cast<float>(hexEscapes) * 0.04f, 0.96f)
    };

  if ((octalEscapes + nullEscapes) >= 4 && escapeRatio >= 0.35f)
    return {
      Language::BinaryData,
      std::min(0.74f + static_cast<float>(octalEscapes + nullEscapes) * 0.04f, 0.96f)
    };

  return {Language::Unknown, 0.0f};
}

/// Detect long hex strings and grouped hex payloads.
static ClassificationResult DetectHexData(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 16)
    return {Language::Unknown, 0.0f};

  int hexDigits = 0;
  int separators = 0;
  for (const char c: trimmed) {
    if (const auto uc = static_cast<unsigned char>(c); std::isxdigit(uc)) {
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

  return {
    Language::HexData,
    std::min(0.75f + static_cast<float>(hexDigits) / 128.0f, 0.97f)
  };
}

/// Detect JSON by leading brace/bracket and "key": patterns.
static ClassificationResult DetectJSON(const std::string_view s) {
  const auto trimmed = TrimLeft(s);
  if (trimmed.empty())
    return {Language::Unknown, 0.0f};
  if (trimmed[0] != '{' && trimmed[0] != '[')
    return {Language::Unknown, 0.0f};

  // Count "key": patterns
  int pairs = 0;
  for (size_t i = 0; i + 2 < s.size(); i++) {
    if (s[i] != '"')
      continue;
    const size_t close = s.find('"', i + 1);
    if (close == std::string_view::npos)
      break;
    const size_t next = s.find_first_not_of(" \t\n\r", close + 1);
    if (next != std::string_view::npos && s[next] == ':') {
      pairs++;
      i = next;
    } else {
      i = close;
    }
  }
  if (pairs == 0)
    return {Language::Unknown, 0.0f};
  return {Language::JSON, std::min(0.6f + static_cast<float>(pairs) * 0.1f, 0.95f)};
}

/// Detect YAML by repeated "key: value" lines or list items.
static ClassificationResult DetectYAML(const std::string_view s) {
  const auto analysis = AnalyzeYAMLStructure(s);
  if (!HasStrongYAMLEvidence(s))
    return {Language::Unknown, 0.0f};

  const int signals = analysis.docMarkers + analysis.keyValueLines +
                      analysis.blockKeyLines + analysis.listLines;
  return {Language::YAML, std::min(0.58f + static_cast<float>(signals) * 0.08f, 0.92f)};
}

/// Detect SQL by leading keyword and supporting keywords.
static ClassificationResult DetectSQL(const std::string_view s) {
  const auto trimmed = TrimLeft(s);
  if (trimmed.size() < 6)
    return {Language::Unknown, 0.0f};

  static constexpr std::string_view leaders[] = {
    "select ", "insert ", "update ", "delete ", "create ",
    "alter ", "drop ", "merge ", "with ", "grant ",
    "revoke ", "begin ", "commit ", "rollback ", "explain ",
  };
  bool hasLeader = false;
  for (const auto leader: leaders) {
    if (StartsWithCI(trimmed, leader)) {
      hasLeader = true;
      break;
    }
  }
  if (!hasLeader)
    return {Language::Unknown, 0.0f};

  // Count supporting keywords for confidence
  static constexpr std::string_view keywords[] = {
    " from ", " where ", " join ", " inner ", " outer ",
    " left ", " right ", " group ", " order ", " having ",
    " limit ", " values", " into ", " set ", " table ",
    " index ", " on ", " and ", " or ", " not ",
    " in ", " like ", " between ", " exists ", " null",
    " as ", " distinct", " union ",
  };
  int kwCount = 0;
  for (const auto kw: keywords) {
    if (FindCI(s, kw) != std::string_view::npos)
      kwCount++;
  }
  return {Language::SQL, std::min(0.65f + static_cast<float>(kwCount) * 0.05f, 0.95f)};
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
    if (s[i] != '<')
      continue;

    const bool isClosing = (i + 1 < s.size() && s[i + 1] == '/');
    const size_t nameStart = i + 1 + (isClosing ? 1 : 0);
    if (nameStart >= s.size() ||
        !std::isalpha(static_cast<unsigned char>(s[nameStart])))
      continue;

    size_t nameEnd = nameStart;
    while (nameEnd < s.size() &&
           std::isalnum(static_cast<unsigned char>(s[nameEnd])))
      nameEnd++;

    if (nameEnd == nameStart)
      continue;
    if (nameEnd < s.size()) {
      const char next = s[nameEnd];
      if (next != ' ' && next != '>' && next != '/' &&
          next != '\t' && next != '\n')
        continue;
    }

    if (IsHTMLTagName(s.substr(nameStart, nameEnd - nameStart))) {
      htmlTags++;
      if (isClosing)
        closingTags++;
    }
  }

  if (htmlTags == 0)
    return {Language::Unknown, 0.0f};
  float confidence = 0.6f;
  if (closingTags > 0)
    confidence += 0.15f;
  confidence += std::min(static_cast<float>(htmlTags) * 0.05f, 0.20f);
  return {Language::HTML, std::min(confidence, 0.95f)};
}

/// Detect XML by prolog, CDATA, or namespaced tags.
static ClassificationResult DetectXML(const std::string_view s) {
  if (s.find("<?xml") != std::string_view::npos)
    return {Language::XML, 0.95f};
  if (s.find("<![CDATA[") != std::string_view::npos)
    return {Language::XML, 0.90f};

  // Look for actual namespaced XML tags, not generic angle-bracket placeholders.
  int nsOpenTags = 0;
  int nsClosingTags = 0;
  int nsSelfClosingTags = 0;
  int attributeAssignments = 0;
  bool hasXmlnsAttribute = false;

  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (s[i] != '<' || s[i + 1] == '!' || s[i + 1] == '?')
      continue;

    const bool isClosing = s[i + 1] == '/';
    const size_t nameStart = i + 1 + (isClosing ? 1 : 0);
    if (nameStart >= s.size() || !IsXMLNameStartChar(s[nameStart]))
      continue;

    size_t nameEnd = nameStart + 1;
    while (nameEnd < s.size() &&
           (IsXMLNameChar(s[nameEnd]) || s[nameEnd] == ':'))
      nameEnd++;

    const auto tagName = s.substr(nameStart, nameEnd - nameStart);
    if (!LooksLikeXMLQualifiedName(tagName))
      continue;

    if (nameEnd < s.size()) {
      const char next = s[nameEnd];
      if (next != '>' && next != '/' && next != ' ' &&
          next != '\t' && next != '\n' && next != '\r')
        continue;
    }

    const size_t tagEnd = s.find('>', nameEnd);
    if (tagEnd == std::string_view::npos)
      continue;

    if (isClosing) {
      nsClosingTags++;
      i = tagEnd;
      continue;
    }

    nsOpenTags++;
    const auto tagTail = s.substr(nameEnd, tagEnd - nameEnd);
    if (!tagTail.empty() && tagTail.find("xmlns") != std::string_view::npos)
      hasXmlnsAttribute = true;
    attributeAssignments += static_cast<int>(
      std::ranges::count(tagTail, '='));
    if (!tagTail.empty()) {
      size_t tailPos = tagTail.size();
      while (tailPos > 0 &&
             std::isspace(static_cast<unsigned char>(tagTail[tailPos - 1])))
        tailPos--;
      if (tailPos > 0 && tagTail[tailPos - 1] == '/')
        nsSelfClosingTags++;
    }
    i = tagEnd;
  }

  if (nsOpenTags == 0)
    return {Language::Unknown, 0.0f};

  if (nsClosingTags == 0 && nsSelfClosingTags == 0 && !hasXmlnsAttribute)
    return {Language::Unknown, 0.0f};

  float confidence = 0.72f;
  confidence += std::min(static_cast<float>(nsOpenTags) * 0.05f, 0.10f);
  if (nsClosingTags > 0)
    confidence += 0.08f;
  if (nsSelfClosingTags > 0)
    confidence += 0.05f;
  if (attributeAssignments > 0)
    confidence += 0.05f;
  if (hasXmlnsAttribute)
    confidence += 0.10f;

  return {Language::XML, std::min(confidence, 0.95f)};
}

/// Detect regex patterns by counting metacharacter signals.
static ClassificationResult DetectRegex(const std::string_view s) {
  if (s.size() < 3)
    return {Language::Unknown, 0.0f};

  int signals = 0;

  // Regex-specific escape sequences
  static constexpr std::string_view escapes[] = {
    "\\d", "\\D", "\\w", "\\W", "\\s", "\\S", "\\b", "\\B",
  };
  for (const auto esc: escapes) {
    if (s.find(esc) != std::string_view::npos)
      signals += 2;
  }

  // Regex-specific groups
  static constexpr std::string_view groups[] = {
    "(?:", "(?=", "(?!", "(?<=", "(?<!", "(?P<", "(?P=",
  };
  for (const auto grp: groups) {
    if (s.find(grp) != std::string_view::npos)
      signals += 3;
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
      if (j < s.size() && s[j] == '}')
        signals += 2;
    }
  }

  // Anchors
  if (s.front() == '^')
    signals++;
  if (s.back() == '$')
    signals++;

  if (signals < 2)
    return {Language::Unknown, 0.0f};
  return {Language::Regex, std::min(0.5f + static_cast<float>(signals) * 0.08f, 0.95f)};
}

/// Detect CSS by scanning for known CSS property names.
static ClassificationResult DetectCSS(const std::string_view s) {
  // Fast reject: CSS needs both { and ;
  if (s.find('{') == std::string_view::npos ||
      s.find(';') == std::string_view::npos)
    return {Language::Unknown, 0.0f};

  static constexpr std::string_view props[] = {
    "color:", "background:", "margin:", "padding:",
    "border:", "display:", "position:", "font-size:",
    "font-family:", "width:", "height:", "top:",
    "left:", "right:", "bottom:", "z-index:",
    "overflow:", "text-align:", "float:", "opacity:",
    "transform:", "transition:", "animation:", "flex:",
    "grid:", "justify-content:", "align-items:",
  };
  int matches = 0;
  for (const auto prop: props) {
    if (FindCI(s, prop) != std::string_view::npos)
      matches++;
  }
  if (matches == 0)
    return {Language::Unknown, 0.0f};
  return {Language::CSS, std::min(0.6f + static_cast<float>(matches) * 0.1f, 0.95f)};
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
      if (!doublePipe)
        signals += 2;
    }
  }

  // Redirections
  if (s.find(">>") != std::string_view::npos)
    signals++;
  if (s.find("2>&1") != std::string_view::npos)
    signals += 2;
  if (s.find("2>/dev/null") != std::string_view::npos)
    signals += 2;

  // Leading commands
  const auto trimmed = TrimLeft(s);
  static constexpr std::string_view cmds[] = {
    "echo ", "cat ", "grep ", "sed ", "awk ", "find ",
    "xargs ", "ls ", "cd ", "mv ", "cp ", "rm ",
    "mkdir ", "chmod ", "chown ", "tar ", "curl ", "wget ",
    "ssh ", "scp ", "git ", "docker ", "make ", "cmake ",
    "pip ", "npm ", "apt ", "yum ", "brew ", "sudo ",
  };
  for (const auto cmd: cmds) {
    if (trimmed.starts_with(cmd)) {
      signals += 2;
      break;
    }
  }

  // Shell variables: $VAR or ${VAR}
  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (s[i] == '$' &&
        (std::isalpha(static_cast<unsigned char>(s[i + 1])) ||
         s[i + 1] == '{' || s[i + 1] == '('))
      signals++;
  }

  if (signals < 2)
    return {Language::Unknown, 0.0f};
  return {Language::Shell, std::min(0.55f + static_cast<float>(signals) * 0.08f, 0.95f)};
}

/// Detect metric names, trace categories, symbol names, and resource-like tokens.
static ClassificationResult DetectIdentifierLike(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 4 || trimmed.size() > 160)
    return {Language::Unknown, 0.0f};

  if (trimmed.find_first_of(" \t\n\r") != std::string_view::npos)
    return {Language::Unknown, 0.0f};
  if (LooksLikeBareDomainLikeToken(trimmed))
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
  for (const char c: trimmed) {
    if (const auto uc = static_cast<unsigned char>(c); std::isalnum(uc)) {
      if (std::isalpha(uc))
        letters++;
      if (std::isdigit(uc))
        digits++;
      if (prevWasSeparator)
        componentStarts++;
      if (std::islower(static_cast<unsigned char>(prev)) &&
          std::isupper(uc))
        camelTransitions++;
      prevWasSeparator = false;
    } else if (IsIdentifierLikeSeparator(c)) {
      separators++;
      prevWasSeparator = true;
      if (c == '.')
        hasDot = true;
      if (c == '/')
        hasSlash = true;
      if (c == ':' && prev == ':')
        hasDoubleColon = true;
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

  return {
    Language::IdentifierLike,
    std::min(0.62f + static_cast<float>(signals) * 0.05f, 0.95f)
  };
}

/// Detect ordinary prose, log lines, and other natural-language fragments.
static ClassificationResult DetectPlainText(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 12)
    return {Language::Unknown, 0.0f};
  if (trimmed.find('@') != std::string_view::npos)
    return {Language::Unknown, 0.0f};

  if (LooksLikeDecoratedPlainText(trimmed))
    return {Language::PlainText, 0.82f};

  int letters = 0;
  int digits = 0;
  int spaces = 0;
  int words = 0;
  int sentenceMarks = 0;
  int codeSymbols = 0;
  bool inWord = false;

  for (const char c: trimmed) {
    const auto uc = static_cast<unsigned char>(c);
    if (std::isalpha(uc))
      letters++;
    if (std::isdigit(uc))
      digits++;
    if (std::isspace(uc))
      spaces++;

    const bool isWord =
        std::isalnum(uc) || c == '\'' || c == '-' || c == '_';
    if (isWord && !inWord)
      words++;
    inWord = isWord;

    if (c == '.' || c == '!' || c == '?' || c == ':')
      sentenceMarks++;
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

  if (codeSymbols > 2)
    return {Language::Unknown, 0.0f};

  const int nonSpace = static_cast<int>(trimmed.size()) - spaces;
  if (nonSpace <= 0)
    return {Language::Unknown, 0.0f};

  const float letterRatio = static_cast<float>(letters) / static_cast<float>(nonSpace);
  const float digitRatio = static_cast<float>(digits) / static_cast<float>(nonSpace);

  if (letterRatio < 0.55f || digitRatio > 0.30f)
    return {Language::Unknown, 0.0f};

  if (LooksLikeShortPlainTextLabel(trimmed))
    return {Language::PlainText, std::min(0.72f + static_cast<float>(words) * 0.04f, 0.88f)};

  if (words >= 4 && (spaces >= 2 || sentenceMarks > 0))
    return {Language::PlainText, std::min(0.70f + static_cast<float>(words) * 0.03f, 0.92f)};

  if (words >= 6)
    return {Language::PlainText, std::min(0.68f + static_cast<float>(words) * 0.025f, 0.90f)};

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

ClassificationResult ClassifyString(
  const std::string_view body,
  const float minConfidence
) {
  if (body.size() < 2)
    return {Language::Unknown, 0.0f};

  if (const auto result = DetectBinaryData(body); result.confidence >= minConfidence)
    return result;

  const std::string normalized = NormalizeForClassification(body);
  const std::string_view text = normalized.empty() ? body : std::string_view(normalized);
  if (LooksLikeBareDomainLikeToken(text))
    return {Language::Unknown, 0.0f};

  // Layer 1: structural detectors (ordered by specificity, lowest FP first)
  using Detector = ClassificationResult (*)(std::string_view);
  static constexpr Detector detectors[] = {
    DetectEmail, DetectURL, DetectXML, DetectFilePath,
    DetectHexData, DetectSQL, DetectJSON,
    DetectHTML, DetectRegex, DetectInlineAsm, DetectFormatString,
    DetectCSS, DetectShell, DetectYAML,
    DetectIdentifierLike, DetectPlainText, DetectSeparatorLine,
  };

  for (const auto detect: detectors) {
    if (const auto result = detect(text); result.confidence >= minConfidence)
      return result;
  }

  // Layer 2: Naive Bayes trigram classifier (requires >= 15 chars).
  // With 30+ competing classes, softmax confidence must be high to be meaningful.
  if (text.size() >= 15) {
    const float nbThreshold = std::max(minConfidence, 0.85f);
    if (const auto result = NaiveBayesClassify(text);
      result.confidence >= nbThreshold) {
      if (result.language == Language::YAML && !HasStrongYAMLEvidence(text))
        return {Language::Unknown, 0.0f};
      return result;
    }
  }

  return {Language::Unknown, 0.0f};
}
