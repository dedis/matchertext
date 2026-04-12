#include "Internal.hpp"

namespace classifier_internal {

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
