#include "Internal.hpp"

#include <vector>

namespace classifier_internal {
  namespace {
    bool IsURLChar(const char c) {
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
    };

    bool IsLikelyDataURL(const std::string_view s) {
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

    std::vector<TokenMatch> FindURLTokens(const std::string_view s) {
      static constexpr std::string_view schemes[] = {
        "http://", "https://", "ftp://", "ftps://", "file://",
        "mailto:", "ssh://", "git://", "svn://", "telnet://",
        "ws://", "wss://", "data:",
      };

      std::vector<TokenMatch> matches;
      size_t pos = 0;
      while (pos < s.size()) {
        std::string_view matchedScheme;
        for (const auto scheme: schemes) {
          if (StartsWithCI(s.substr(pos), scheme)) {
            matchedScheme = scheme;
            break;
          }
        }

        if (matchedScheme.empty()) {
          pos++;
          continue;
        }

        if (pos > 0 && IsURLChar(s[pos - 1])) {
          pos++;
          continue;
        }

        if (matchedScheme == "data:" && !IsLikelyDataURL(s.substr(pos))) {
          pos++;
          continue;
        }

        size_t end = pos + matchedScheme.size();
        while (end < s.size() && IsURLChar(s[end]))
          end++;
        while (end > pos + matchedScheme.size() &&
               (s[end - 1] == '.' || s[end - 1] == ',' || s[end - 1] == ';' ||
                s[end - 1] == ':' || s[end - 1] == '!' || s[end - 1] == '?'))
          end--;

        if (end <= pos + matchedScheme.size()) {
          pos++;
          continue;
        }

        matches.push_back({pos, end});
        pos = end;
      }

      return matches;
    }
  } // namespace

  ClassificationResult DetectURL(const std::string_view s) {
    const auto matches = FindURLTokens(s);
    if (matches.empty())
      return {Language::Unknown, 0.0f};

    const auto &first = matches.front();
    const auto prefix = s.substr(0, first.start);
    const auto suffix = s.substr(first.end);
    if (matches.size() == 1 && prefix.empty() && suffix.empty())
      return {Language::URL, 0.95f};

    return {Language::PseudoURL, matches.size() > 1 ? 0.92f : 0.88f};
  }
} // namespace classifier_internal
