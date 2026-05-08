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
    if (trimmed.size() < 12 || trimmed.find('@') != std::string_view::npos)
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

      const bool isWord = std::isalnum(uc) || c == '\'' || c == '-' || c == '_';
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
} // namespace classifier_internal
