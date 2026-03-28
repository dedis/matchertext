#include "Internal.hpp"

namespace classifier_internal {

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
