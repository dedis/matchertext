#include "Internal.hpp"

namespace classifier_internal {

ClassificationResult DetectHTML(const std::string_view s) {
  if (s.find('<') == std::string_view::npos)
    return {Language::Unknown, 0.0f};

  if (FindCI(s, "<!doctype") != std::string_view::npos)
    return {Language::HTML, 0.95f};

  int htmlTags = 0;
  int closingTags = 0;

  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '<')
      continue;

    const bool isClosing = (i + 1 < s.size() && s[i + 1] == '/');
    const size_t nameStart = i + 1 + (isClosing ? 1 : 0);
    if (nameStart >= s.size() ||
        !std::isalpha(static_cast<unsigned char>(s[nameStart])))
      continue;

    size_t nameEnd = nameStart;
    while (nameEnd < s.size() &&
           std::isalnum(static_cast<unsigned char>(s[nameEnd])))
      nameEnd++;

    if (nameEnd == nameStart)
      continue;
    if (nameEnd < s.size()) {
      const char next = s[nameEnd];
      if (next != ' ' && next != '>' && next != '/' &&
          next != '\t' && next != '\n')
        continue;
    }

    if (IsHTMLTagName(s.substr(nameStart, nameEnd - nameStart))) {
      htmlTags++;
      if (isClosing)
        closingTags++;
    }
  }

  if (htmlTags == 0)
    return {Language::Unknown, 0.0f};
  float confidence = 0.6f;
  if (closingTags > 0)
    confidence += 0.15f;
  confidence += std::min(static_cast<float>(htmlTags) * 0.05f, 0.20f);
  return {Language::HTML, std::min(confidence, 0.95f)};
}

} // namespace classifier_internal
