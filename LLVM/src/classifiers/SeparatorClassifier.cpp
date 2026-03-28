#include "Internal.hpp"

namespace classifier_internal {

ClassificationResult DetectSeparatorLine(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 8)
    return {Language::Unknown, 0.0f};

  int letters = 0;
  int digits = 0;
  int maxRepeatedRun = 1;
  int shortAlphaRun = 0;
  int maxAlphaRun = 0;

  for (const char c: trimmed) {
    const auto uc = static_cast<unsigned char>(c);
    if (std::isalpha(uc))
      letters++;
    else if (std::isdigit(uc))
      digits++;
    if (std::isalpha(uc)) {
      shortAlphaRun++;
      maxAlphaRun = std::max(maxAlphaRun, shortAlphaRun);
    } else {
      shortAlphaRun = 0;
    }
  }

  if (!HasRepeatedNonLetterRun(trimmed, &maxRepeatedRun))
    return {Language::Unknown, 0.0f};

  if (letters > 0 || digits > 0) {
    if (digits == 0 && letters <= 2 && maxAlphaRun <= 1)
      return {Language::Unknown, 1.0f};
    return {Language::Unknown, 0.0f};
  }

  if (maxRepeatedRun < 5)
    return {Language::Unknown, 0.0f};

  return {Language::Unknown, 1.0f};
}

} // namespace classifier_internal
