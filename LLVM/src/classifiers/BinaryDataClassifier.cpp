#include "Internal.hpp"

namespace classifier_internal {

ClassificationResult DetectBinaryData(const std::string_view s) {
  if (s.size() < 8)
    return {Language::Unknown, 0.0f};

  int controlBytes = 0;
  int escapedBytes = 0;
  int hexEscapes = 0;
  int octalEscapes = 0;
  int nullEscapes = 0;
  int letters = 0;
  int spaces = 0;

  for (size_t i = 0; i < s.size(); i++) {
    const auto uc = static_cast<unsigned char>(s[i]);
    if (std::isalpha(uc))
      letters++;
    if (std::isspace(uc))
      spaces++;
    if (uc < 0x20 && s[i] != '\n' && s[i] != '\r' && s[i] != '\t')
      controlBytes++;

    if (s[i] != '\\' || i + 1 >= s.size())
      continue;

    const char next = s[i + 1];
    if (next == 'x' || next == 'X') {
      size_t j = i + 2;
      int digits = 0;
      while (j < s.size() && IsHexDigit(s[j])) {
        digits++;
        j++;
      }
      if (digits >= 2) {
        escapedBytes++;
        hexEscapes++;
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
      escapedBytes++;
      nullEscapes++;
      if (digits > 1)
        octalEscapes++;
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
        escapedBytes++;
        octalEscapes++;
        i = j - 1;
        continue;
      }
    }
  }

  const int strongEscapes = hexEscapes + octalEscapes + nullEscapes;
  const float controlRatio =
      static_cast<float>(controlBytes) / static_cast<float>(std::max<size_t>(s.size(), 1));
  const float escapeRatio =
      static_cast<float>(escapedBytes * 4) / static_cast<float>(std::max<size_t>(s.size(), 1));

  if (controlBytes >= 4 && controlRatio >= 0.08f)
    return {Language::BinaryData, std::min(0.72f + controlRatio, 0.96f)};

  if (strongEscapes >= 4 && letters <= 8 && spaces == 0)
    return {
      Language::BinaryData,
      std::min(0.76f + static_cast<float>(strongEscapes) * 0.03f, 0.97f)
    };

  if (hexEscapes >= 3 && escapeRatio >= 0.40f)
    return {
      Language::BinaryData,
      std::min(0.74f + static_cast<float>(hexEscapes) * 0.04f, 0.96f)
    };

  if ((octalEscapes + nullEscapes) >= 4 && escapeRatio >= 0.35f)
    return {
      Language::BinaryData,
      std::min(0.74f + static_cast<float>(octalEscapes + nullEscapes) * 0.04f, 0.96f)
    };

  return {Language::Unknown, 0.0f};
}

} // namespace classifier_internal
