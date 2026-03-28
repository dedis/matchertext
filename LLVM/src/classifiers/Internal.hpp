#pragma once

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>

#include "../../include/LanguageClassifier.hpp"

namespace classifier_internal {

inline char ToLower(const char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline std::string_view TrimLeft(const std::string_view s) {
  const size_t pos = s.find_first_not_of(" \t\n\r");
  return pos == std::string_view::npos ? std::string_view{} : s.substr(pos);
}

inline std::string_view TrimRight(const std::string_view s) {
  const size_t pos = s.find_last_not_of(" \t\n\r");
  return pos == std::string_view::npos ? std::string_view{} : s.substr(0, pos + 1);
}

inline std::string_view Trim(const std::string_view s) {
  return TrimRight(TrimLeft(s));
}

inline bool StartsWithCI(const std::string_view s, const std::string_view prefix) {
  if (s.size() < prefix.size())
    return false;
  for (size_t i = 0; i < prefix.size(); i++) {
    if (ToLower(s[i]) != ToLower(prefix[i]))
      return false;
  }
  return true;
}

inline size_t FindCI(const std::string_view haystack, const std::string_view needle) {
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

inline std::string StripCommentDecorators(const std::string_view body) {
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

inline std::string UnescapeCommonSequences(const std::string_view body) {
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

inline std::string NormalizeForClassification(const std::string_view body) {
  return UnescapeCommonSequences(StripCommentDecorators(body));
}

inline bool IsSimpleKey(const std::string_view s) {
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

inline bool LooksLikeYAMLKey(const std::string_view key) {
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

inline bool LooksLikeYAMLScalarValue(const std::string_view value) {
  const auto trimmed = Trim(value);
  if (trimmed.empty() || trimmed.size() > 80)
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

inline bool LooksLikeYAMLListItem(const std::string_view item) {
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

inline YAMLAnalysis AnalyzeYAMLStructure(const std::string_view s) {
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

inline bool HasStrongYAMLEvidence(const std::string_view s) {
  const auto [docMarkers, keyValueLines, blockKeyLines, listLines, nonEmptyLines] =
      AnalyzeYAMLStructure(s);
  if (nonEmptyLines == 0)
    return false;

  if (docMarkers > 0 && keyValueLines + blockKeyLines + listLines >= 2)
    return true;
  if (blockKeyLines > 0 && (listLines > 0 || keyValueLines > 0))
    return true;
  return false;
}

inline bool IsIdentifierLikeSeparator(const char c) {
  return c == '_' || c == '-' || c == '.' || c == '/' || c == ':';
}

inline bool IsHexDigit(const char c) {
  return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

inline bool IsOctalDigit(const char c) {
  return c >= '0' && c <= '7';
}

inline bool IsLabelPunctuation(const char c) {
  return c == '.' || c == ',' || c == ':' || c == ';' || c == '!' ||
         c == '?' || c == '-' || c == '(' || c == ')';
}

inline bool IsCodeKeywordToken(const std::string_view token) {
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

inline bool LooksLikeShortPlainTextLabel(const std::string_view s) {
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

  const float letterRatio = static_cast<float>(letters) / static_cast<float>(nonSpace);
  return letterRatio >= 0.65f;
}

inline bool HasRepeatedNonLetterRun(const std::string_view s, int *maxRunLength = nullptr) {
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

inline bool IsHumanWordToken(const std::string_view token) {
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

inline std::string ExtractDecoratedTextCore(const std::string_view s) {
  std::string core;
  size_t pos = 0;

  while (pos <= s.size()) {
    const size_t lineEnd = s.find('\n', pos);
    auto line = Trim(
      s.substr(
        pos, lineEnd == std::string_view::npos ? s.size() - pos : lineEnd - pos
      )
    );

    while (!line.empty() && !std::isalnum(static_cast<unsigned char>(line.front())))
      line.remove_prefix(1);
    while (!line.empty() && !std::isalnum(static_cast<unsigned char>(line.back())))
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

inline bool LooksLikeDecoratedPlainText(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 8 || !HasRepeatedNonLetterRun(trimmed))
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

  if (codeSymbols > 2 || alphaWords == 0 || digits > letters / 2)
    return false;
  return words >= 2 && alphaWords >= 1;
}

inline bool IsHTMLTagName(const std::string_view name) {
  if (name.empty() || name.size() > 10)
    return false;

  char lower[11];
  for (size_t i = 0; i < name.size(); i++)
    lower[i] = ToLower(name[i]);
  const std::string_view lName(lower, name.size());

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

inline bool IsXMLNameStartChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalpha(uc) || c == '_';
}

inline bool IsXMLNameChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '_' || c == '-' || c == '.';
}

inline bool LooksLikeXMLQualifiedName(const std::string_view name) {
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

inline bool IsURLChar(const char c) {
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

inline std::string_view StripURLWrappers(const std::string_view s) {
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

inline bool IsLikelyDataURL(const std::string_view s) {
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

inline bool LooksLikeBriefURLContext(const std::string_view s) {
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

inline TokenMatch FindSingleURLToken(const std::string_view s) {
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

inline bool IsEmailLocalChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-';
}

inline bool IsEmailDomainChar(const char c) {
  const auto uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '.' || c == '-';
}

inline bool IsLikelyEmailToken(const std::string_view token) {
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

inline bool LooksLikeBareDomainLikeToken(const std::string_view s) {
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
  if (const int dotCount = static_cast<int>(std::ranges::count(trimmed, '.'));
      dotCount < 2 && !(tld.size() >= 2 && tld.size() <= 4))
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

inline TokenMatch FindSingleEmailToken(const std::string_view s) {
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

inline bool HasTightPunctuationContinuation(const std::string_view side) {
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

inline bool IsOnlyTerminalPunctuation(const std::string_view s) {
  if (s.empty())
    return false;
  return std::ranges::all_of(s, [](const char c) {
    return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?';
  });
}

ClassificationResult DetectURL(std::string_view s);
ClassificationResult DetectEmail(std::string_view s);
ClassificationResult DetectFilePath(std::string_view s);
ClassificationResult DetectFormatString(std::string_view s);
ClassificationResult DetectInlineAsm(std::string_view s);
ClassificationResult DetectBinaryData(std::string_view s);
ClassificationResult DetectHexData(std::string_view s);
ClassificationResult DetectJSON(std::string_view s);
ClassificationResult DetectYAML(std::string_view s);
ClassificationResult DetectSQL(std::string_view s);
ClassificationResult DetectHTML(std::string_view s);
ClassificationResult DetectXML(std::string_view s);
ClassificationResult DetectRegex(std::string_view s);
ClassificationResult DetectCSS(std::string_view s);
ClassificationResult DetectShell(std::string_view s);
ClassificationResult DetectIdentifierLike(std::string_view s);
ClassificationResult DetectPlainText(std::string_view s);
ClassificationResult DetectSeparatorLine(std::string_view s);

} // namespace classifier_internal
