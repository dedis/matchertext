#include "Internal.hpp"

namespace classifier_internal {

ClassificationResult DetectYAML(const std::string_view s) {
  const auto analysis = AnalyzeYAMLStructure(s);
  if (!HasStrongYAMLEvidence(s))
    return {Language::Unknown, 0.0f};

  const int signals = analysis.docMarkers + analysis.keyValueLines +
                      analysis.blockKeyLines + analysis.listLines;
  return {Language::YAML, std::min(0.58f + static_cast<float>(signals) * 0.08f, 0.92f)};
}

} // namespace classifier_internal
