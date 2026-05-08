#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsHexDigit(const char c) {
      return std::isxdigit(static_cast<unsigned char>(c)) != 0;
    }

    bool IsOctalDigit(const char c) {
      return c >= '0' && c <= '7';
    }
  } // namespace

  BinaryDataAnalysis AnalyzeBinaryData(const std::string_view s) {
    BinaryDataAnalysis analysis;

    for (size_t i = 0; i < s.size(); i++) {
      const auto uc = static_cast<unsigned char>(s[i]);
      if (std::isalpha(uc))
        analysis.letters++;
      if (std::isspace(uc))
        analysis.spaces++;
      if (uc < 0x20 && s[i] != '\n' && s[i] != '\r' && s[i] != '\t')
        analysis.controlBytes++;

      if (s[i] != '\\' || i + 1 >= s.size()) {
        if (std::isprint(uc) && !std::isspace(uc))
          analysis.visiblePrintableChars++;
        continue;
      }

      const char next = s[i + 1];
      if (next == 'x' || next == 'X') {
        size_t j = i + 2;
        int digits = 0;
        while (j < s.size() && IsHexDigit(s[j])) {
          digits++;
          j++;
        }
        if (digits >= 2) {
          analysis.escapedBytes++;
          analysis.hexEscapes++;
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
        analysis.escapedBytes++;
        analysis.nullEscapes++;
        if (digits > 1)
          analysis.octalEscapes++;
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
          analysis.escapedBytes++;
          analysis.octalEscapes++;
          i = j - 1;
          continue;
        }
      }
    }

    return analysis;
  }

  bool HasStrongBinaryEvidence(
    const BinaryDataAnalysis &analysis,
    const std::string_view s
  ) {
    const int strongEscapes = analysis.StrongEscapes();
    const float controlRatio = analysis.ControlRatio(s.size());
    const float escapeRatio = analysis.EscapeRatio(s.size());

    return (analysis.controlBytes >= 4 && controlRatio >= 0.08f) ||
           (strongEscapes >= 4 && analysis.letters <= 8 && analysis.spaces == 0) ||
           (analysis.hexEscapes >= 3 && escapeRatio >= 0.40f) ||
           ((analysis.octalEscapes + analysis.nullEscapes) >= 4 &&
            escapeRatio >= 0.35f);
  }

  bool HasReadableBinaryContext(const BinaryDataAnalysis &analysis) {
    return analysis.visiblePrintableChars > 0;
  }

  ClassificationResult DetectBinaryData(const std::string_view s) {
    if (s.size() < 8)
      return {Language::Unknown, 0.0f};

    const auto analysis = AnalyzeBinaryData(s);
    if (!HasStrongBinaryEvidence(analysis, s) || HasReadableBinaryContext(analysis))
      return {Language::Unknown, 0.0f};

    const int strongEscapes = analysis.StrongEscapes();
    const float controlRatio = analysis.ControlRatio(s.size());
    const float escapeRatio = analysis.EscapeRatio(s.size());

    if (analysis.controlBytes >= 4 && controlRatio >= 0.08f)
      return {Language::BinaryData, std::min(0.72f + controlRatio, 0.96f)};

    if (strongEscapes >= 4 && analysis.letters <= 8 && analysis.spaces == 0)
      return {
        Language::BinaryData,
        std::min(0.76f + static_cast<float>(strongEscapes) * 0.03f, 0.97f)
      };

    if (analysis.hexEscapes >= 3 && escapeRatio >= 0.40f)
      return {
        Language::BinaryData,
        std::min(0.74f + static_cast<float>(analysis.hexEscapes) * 0.04f, 0.96f)
      };

    if ((analysis.octalEscapes + analysis.nullEscapes) >= 4 && escapeRatio >= 0.35f)
      return {
        Language::BinaryData,
        std::min(
          0.74f + static_cast<float>(analysis.octalEscapes + analysis.nullEscapes) * 0.04f,
          0.96f
        )
      };

    return {Language::Unknown, 0.0f};
  }
} // namespace classifier_internal
