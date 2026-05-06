#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsSimpleKey(const std::string_view s) {
      if (s.empty())
        return false;
      return std::ranges::all_of(
        s, [](const unsigned char c) {
          return std::isalnum(c) || c == '_' || c == '-' || c == '.';
        }
      );
    }

    bool LooksLikeYAMLKey(const std::string_view key) {
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

    bool LooksLikeYAMLScalarValue(const std::string_view value) {
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

    bool LooksLikeYAMLListItem(const std::string_view item) {
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
  } // namespace

  YAMLAnalysis AnalyzeYAMLStructure(const std::string_view s) {
    YAMLAnalysis analysis;

    size_t pos = 0;
    while (pos <= s.size()) {
      const size_t end = s.find('\n', pos);
      const size_t lineEnd = end == std::string_view::npos ? s.size() : end;

      if (const auto line = Trim(s.substr(pos, lineEnd - pos)); !line.empty()) {
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

  bool HasStrongYAMLEvidence(const std::string_view s) {
    return HasStrongYAMLEvidence(AnalyzeYAMLStructure(s));
  }

  bool HasStrongYAMLEvidence(const YAMLAnalysis &analysis) {
    if (analysis.nonEmptyLines == 0)
      return false;

    if (analysis.docMarkers > 0 &&
        analysis.keyValueLines + analysis.blockKeyLines + analysis.listLines >= 2)
      return true;
    if (analysis.blockKeyLines > 0 &&
        (analysis.listLines > 0 || analysis.keyValueLines > 0))
      return true;
    return false;
  }

  ClassificationResult DetectYAML(const std::string_view s) {
    const auto analysis = AnalyzeYAMLStructure(s);
    if (!HasStrongYAMLEvidence(analysis))
      return {Language::Unknown, 0.0f};

    const int signals = analysis.docMarkers + analysis.keyValueLines +
                        analysis.blockKeyLines + analysis.listLines;
    return {Language::YAML, std::min(0.58f + static_cast<float>(signals) * 0.08f, 0.92f)};
  }
} // namespace classifier_internal
