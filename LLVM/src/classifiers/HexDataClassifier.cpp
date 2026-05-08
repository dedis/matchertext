#include "Internal.hpp"

namespace classifier_internal {
  ClassificationResult DetectHexData(const std::string_view s) {
    const auto trimmed = Trim(s);
    if (trimmed.size() < 16)
      return {Language::Unknown, 0.0f};

    int hexDigits = 0;
    int separators = 0;
    for (const char c: trimmed) {
      if (const auto uc = static_cast<unsigned char>(c); std::isxdigit(uc)) {
        hexDigits++;
        continue;
      }
      if (c == ' ' || c == ':' || c == '-' || c == '_') {
        separators++;
        continue;
      }
      return {Language::Unknown, 0.0f};
    }

    const int significant = hexDigits + separators;
    if (hexDigits < 16 || significant == 0)
      return {Language::Unknown, 0.0f};

    const float hexRatio = static_cast<float>(hexDigits) / static_cast<float>(significant);
    if (hexRatio < 0.85f)
      return {Language::Unknown, 0.0f};

    const bool evenDigits = (hexDigits % 2) == 0;
    if (!evenDigits && separators == 0)
      return {Language::Unknown, 0.0f};

    return {
      Language::HexData,
      std::min(0.75f + static_cast<float>(hexDigits) / 128.0f, 0.97f)
    };
  }
} // namespace classifier_internal
