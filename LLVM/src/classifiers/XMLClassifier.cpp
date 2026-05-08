#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsXMLNameStartChar(const char c) {
      const auto uc = static_cast<unsigned char>(c);
      return std::isalpha(uc) || c == '_';
    }

    bool IsXMLNameChar(const char c) {
      const auto uc = static_cast<unsigned char>(c);
      return std::isalnum(uc) || c == '_' || c == '-' || c == '.';
    }

    bool LooksLikeXMLQualifiedName(const std::string_view name) {
      if (name.empty() || name.find("::") != std::string_view::npos)
        return false;

      const size_t colon = name.find(':');
      if (colon == std::string_view::npos || colon == 0 || colon + 1 >= name.size())
        return false;
      if (name.find(':', colon + 1) != std::string_view::npos)
        return false;

      const auto prefix = name.substr(0, colon);
      const auto local = name.substr(colon + 1);
      if (!IsXMLNameStartChar(prefix.front()) || !IsXMLNameStartChar(local.front()))
        return false;

      return std::ranges::all_of(prefix.substr(1), IsXMLNameChar) &&
             std::ranges::all_of(local.substr(1), IsXMLNameChar);
    }
  } // namespace

  ClassificationResult DetectXML(const std::string_view s) {
    if (s.find("<?xml") != std::string_view::npos)
      return {Language::XML, 0.95f};
    if (s.find("<![CDATA[") != std::string_view::npos)
      return {Language::XML, 0.90f};

    int nsOpenTags = 0;
    int nsClosingTags = 0;
    int nsSelfClosingTags = 0;
    int attributeAssignments = 0;
    bool hasXmlnsAttribute = false;

    for (size_t i = 0; i + 1 < s.size(); i++) {
      if (s[i] != '<' || s[i + 1] == '!' || s[i + 1] == '?')
        continue;

      const bool isClosing = s[i + 1] == '/';
      const size_t nameStart = i + 1 + (isClosing ? 1 : 0);
      if (nameStart >= s.size() || !IsXMLNameStartChar(s[nameStart]))
        continue;

      size_t nameEnd = nameStart + 1;
      while (nameEnd < s.size() &&
             (IsXMLNameChar(s[nameEnd]) || s[nameEnd] == ':'))
        nameEnd++;

      const auto tagName = s.substr(nameStart, nameEnd - nameStart);
      if (!LooksLikeXMLQualifiedName(tagName))
        continue;

      if (nameEnd < s.size()) {
        const char next = s[nameEnd];
        if (next != '>' && next != '/' && next != ' ' &&
            next != '\t' && next != '\n' && next != '\r')
          continue;
      }

      const size_t tagEnd = s.find('>', nameEnd);
      if (tagEnd == std::string_view::npos)
        continue;

      if (isClosing) {
        nsClosingTags++;
        i = tagEnd;
        continue;
      }

      nsOpenTags++;
      const auto tagTail = s.substr(nameEnd, tagEnd - nameEnd);
      if (!tagTail.empty() && tagTail.find("xmlns") != std::string_view::npos)
        hasXmlnsAttribute = true;
      attributeAssignments += static_cast<int>(std::ranges::count(tagTail, '='));
      if (!tagTail.empty()) {
        size_t tailPos = tagTail.size();
        while (tailPos > 0 &&
               std::isspace(static_cast<unsigned char>(tagTail[tailPos - 1])))
          tailPos--;
        if (tailPos > 0 && tagTail[tailPos - 1] == '/')
          nsSelfClosingTags++;
      }
      i = tagEnd;
    }

    if (nsOpenTags == 0)
      return {Language::Unknown, 0.0f};
    if (nsClosingTags == 0 && nsSelfClosingTags == 0 && !hasXmlnsAttribute)
      return {Language::Unknown, 0.0f};

    float confidence = 0.72f;
    confidence += std::min(static_cast<float>(nsOpenTags) * 0.05f, 0.10f);
    if (nsClosingTags > 0)
      confidence += 0.08f;
    if (nsSelfClosingTags > 0)
      confidence += 0.05f;
    if (attributeAssignments > 0)
      confidence += 0.05f;
    if (hasXmlnsAttribute)
      confidence += 0.10f;

    return {Language::XML, std::min(confidence, 0.95f)};
  }
} // namespace classifier_internal
