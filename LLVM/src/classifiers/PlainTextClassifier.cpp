#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsLabelPunctuation(const char c) {
      return c == '.' || c == ',' || c == ':' || c == ';' || c == '!' ||
             c == '?' || c == '-' || c == '(' || c == ')';
    }

    bool IsCodeKeywordToken(const std::string_view token) {
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

    bool IsHumanWordToken(const std::string_view token) {
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

    std::string ExtractDecoratedTextCore(const std::string_view s) {
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

    bool LooksLikeDecoratedPlainText(const std::string_view s) {
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

    /// A short phrase of a few words (Not run, TEST PASSED, 300 values): mostly
    /// words of letters, any script, with no code symbols. Hyphenated and dotted
    /// tokens count against it, so class lists (grid gap-2) and selectors stay out.
    bool LooksLikeShortPhrase(const std::string_view s) {
      if (s.find_first_of("{}[]<>=;$|&`\\\"#*~^%") != std::string_view::npos)
        return false;

      int words = 0;
      int others = 0;
      int tokens = 0;
      bool keywordsOnly = true;
      size_t pos = 0;
      while (pos < s.size()) {
        const size_t end = std::min(s.find_first_of(" \t\n\r", pos), s.size());
        auto token = s.substr(pos, end - pos);
        pos = end + 1;
        while (!token.empty() && (token.front() == '(' || token.front() == '\''))
          token.remove_prefix(1);
        while (!token.empty() && (IsLabelPunctuation(token.back()) || token.back() == '\''))
          token.remove_suffix(1);
        if (token.empty())
          continue;
        tokens++;

        int letters = 0;
        int digits = 0;
        int rest = 0;
        for (size_t i = 0; i < token.size();) {
          const char c = token[i];
          const uint32_t cp = DecodeCodePoint(token, i);
          if (IsLetterCodePoint(cp))
            letters++;
          else if (cp < 0x80 && std::isdigit(static_cast<unsigned char>(c)))
            digits++;
          else if (!(cp < 0x80 && (c == '\'' || c == '.' || c == ',')))
            rest++;
        }
        if (letters > 0 && digits == 0 && rest == 0) {
          words++;
          if (!IsCodeKeywordToken(token))
            keywordsOnly = false;
        } else if (letters == 0 && rest == 0) {
          // a number: neutral
        } else {
          others++;
        }
      }
      return tokens >= 2 && tokens <= 8 && words > others && !keywordsOnly;
    }
  } // namespace

  bool LooksLikeShortPlainTextLabel(const std::string_view s) {
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

  bool HasRepeatedNonLetterRun(const std::string_view s, int *maxRunLength) {
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

  ClassificationResult DetectPlainText(const std::string_view s) {
    const auto trimmed = Trim(s);
    if (trimmed.find('@') != std::string_view::npos)
      return {LanguageEnum::Unknown, 0.0f};

    if (LooksLikeShortPhrase(trimmed))
      return {LanguageEnum::PlainText, 0.80f};
    if (trimmed.size() < 12)
      return {LanguageEnum::Unknown, 0.0f};

    if (LooksLikeDecoratedPlainText(trimmed))
      return {LanguageEnum::PlainText, 0.82f};

    /// Counted per code point, so letters of any script count as letters.
    int letters = 0;
    int digits = 0;
    int spaces = 0;
    int words = 0;
    int sentenceMarks = 0;
    int codeSymbols = 0;
    int codePoints = 0;
    bool inWord = false;
    char previous = '\0';

    for (size_t pos = 0; pos < trimmed.size();) {
      const char c = trimmed[pos];
      const uint32_t cp = DecodeCodePoint(trimmed, pos);
      codePoints++;
      const bool letter = IsLetterCodePoint(cp);
      const bool ascii = cp < 0x80;
      if (letter)
        letters++;
      if (ascii && std::isdigit(static_cast<unsigned char>(c)))
        digits++;
      if (ascii && std::isspace(static_cast<unsigned char>(c)))
        spaces++;

      const bool isWord = letter || (ascii && (std::isalnum(static_cast<unsigned char>(c)) || c == '\'' || c == '-' || c == '_'));
      if (isWord && !inWord)
        words++;
      inWord = isWord;

      if (!ascii) {
        previous = '\0';
        continue;
      }
      if (c == '.' || c == '!' || c == '?' || c == ':')
        sentenceMarks++;
      /// A backtick run (``code``) counts once.
      if (c == '{' || c == '}' || c == '[' || c == ']' || c == '<' || c == '>' ||
          c == ';' || c == '=' || c == '$' || c == '|' || c == '&' || c == '@' ||
          (c == '`' && previous != '`'))
        codeSymbols++;
      previous = c;
    }

    /// Code markers reject short text outright; long prose such as a docstring
    /// may quote a little code (``x``, :class:`Y`, a -> b), so there they only
    /// count against its symbol budget.
    int codeMarkers = 0;
    for (const auto marker: {"::", "->", "=>", ":=", "=="})
      for (size_t at = trimmed.find(marker); at != std::string_view::npos; at = trimmed.find(marker, at + 2))
        codeMarkers++;
    const bool prose = words >= 12;
    if (codeMarkers > 0 && !prose)
      return {LanguageEnum::Unknown, 0.0f};
    if (prose ? (codeSymbols + 2 * codeMarkers) * 6 > words : codeSymbols > 2)
      return {LanguageEnum::Unknown, 0.0f};

    const int nonSpace = codePoints - spaces;
    if (nonSpace <= 0)
      return {LanguageEnum::Unknown, 0.0f};

    const float letterRatio = static_cast<float>(letters) / static_cast<float>(nonSpace);
    const float digitRatio = static_cast<float>(digits) / static_cast<float>(nonSpace);
    if (letterRatio < 0.55f || digitRatio > 0.30f)
      return {LanguageEnum::Unknown, 0.0f};

    if (LooksLikeShortPlainTextLabel(trimmed))
      return {LanguageEnum::PlainText, std::min(0.72f + static_cast<float>(words) * 0.04f, 0.88f)};
    if (words >= 4 && (spaces >= 2 || sentenceMarks > 0))
      return {LanguageEnum::PlainText, std::min(0.70f + static_cast<float>(words) * 0.03f, 0.92f)};
    if (words >= 6)
      return {LanguageEnum::PlainText, std::min(0.68f + static_cast<float>(words) * 0.025f, 0.90f)};

    return {LanguageEnum::Unknown, 0.0f};
  }
} // namespace classifier_internal
