#include "Internal.hpp"

namespace classifier_internal {
  ClassificationResult DetectPseudoBinaryData(const std::string_view s) {
    if (s.size() < 8)
      return {Language::Unknown, 0.0f};

    const auto analysis = AnalyzeBinaryData(s);
    if (!HasStrongBinaryEvidence(analysis, s) || !HasReadableBinaryContext(analysis))
      return {Language::Unknown, 0.0f};

    const int strongEscapes = analysis.StrongEscapes();
    const float controlRatio = analysis.ControlRatio(s.size());
    const float escapeRatio = analysis.EscapeRatio(s.size());

    float confidence = 0.80f;
    if (analysis.controlBytes >= 4 && controlRatio >= 0.08f)
      confidence = std::max(confidence, std::min(0.74f + controlRatio, 0.90f));
    if (analysis.hexEscapes >= 3 && escapeRatio >= 0.40f)
      confidence = std::max(
        confidence,
        std::min(0.76f + static_cast<float>(analysis.hexEscapes) * 0.03f, 0.92f)
      );
    if ((analysis.octalEscapes + analysis.nullEscapes) >= 4 && escapeRatio >= 0.35f)
      confidence = std::max(
        confidence,
        std::min(
          0.76f + static_cast<float>(analysis.octalEscapes + analysis.nullEscapes) * 0.03f,
          0.92f
        )
      );
    if (strongEscapes >= 4)
      confidence = std::max(
        confidence, std::min(0.78f + static_cast<float>(strongEscapes) * 0.02f, 0.93f)
      );

    return {Language::PseudoBinaryData, confidence};
  }
} // namespace classifier_internal
