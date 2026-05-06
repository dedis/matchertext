#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsEmailLocalChar(const char c) {
      const auto uc = static_cast<unsigned char>(c);
      return std::isalnum(uc) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-';
    }

    bool IsEmailDomainChar(const char c) {
      const auto uc = static_cast<unsigned char>(c);
      return std::isalnum(uc) || c == '.' || c == '-';
    }

    bool IsLikelyEmailToken(const std::string_view token) {
      const size_t at = token.find('@');
      if (at == std::string_view::npos || at == 0 || at + 1 >= token.size())
        return false;
      if (token.find('@', at + 1) != std::string_view::npos)
        return false;

      const auto local = token.substr(0, at);
      const auto domain = token.substr(at + 1);
      if (local.empty() || domain.empty())
        return false;

      if (!std::ranges::all_of(
        local, [](const char c) {
          return IsEmailLocalChar(c);
        }
      ))
        return false;
      if (local.front() == '.' || local.back() == '.')
        return false;
      if (local.find("..") != std::string_view::npos)
        return false;

      if (!std::ranges::all_of(
        domain, [](const char c) {
          return IsEmailDomainChar(c);
        }
      ))
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
        return std::ranges::all_of(
          finalLabel.substr(4), [](const char c) {
            const auto uc = static_cast<unsigned char>(c);
            return std::isalnum(uc) || c == '-';
          }
        );
      }

      return std::ranges::all_of(
        finalLabel, [](const char c) {
          return std::isalpha(static_cast<unsigned char>(c));
        }
      );
    }

    struct EmailTokenMatch {
      size_t start = std::string_view::npos;
      size_t end = std::string_view::npos;
      int count = 0;
    };

    std::string_view StripURLWrappers(const std::string_view s) {
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

    bool LooksLikeBriefURLContext(const std::string_view s) {
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

    EmailTokenMatch FindSingleEmailToken(const std::string_view s) {
      EmailTokenMatch match;

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

    bool HasTightPunctuationContinuation(const std::string_view side) {
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

    bool IsOnlyTerminalPunctuation(const std::string_view s) {
      if (s.empty())
        return false;
      return std::ranges::all_of(
        s, [](const char c) {
          return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?';
        }
      );
    }
  } // namespace

  bool LooksLikeBareDomainLikeToken(const std::string_view s) {
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

  ClassificationResult DetectEmail(const std::string_view s) {
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
} // namespace classifier_internal
