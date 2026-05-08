#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsHTMLTagName(const std::string_view name) {
      if (name.empty() || name.size() > 10)
        return false;

      char lower[11];
      for (size_t i = 0; i < name.size(); i++)
        lower[i] = ToLower(name[i]);
      const std::string_view lName(lower, name.size());

      static constexpr std::string_view tags[] = {
        "a", "abbr", "address", "article", "aside", "b",
        "body", "br", "button", "canvas", "caption", "code",
        "col", "dd", "details", "div", "dl", "dt",
        "em", "fieldset", "figure", "footer", "form", "h1",
        "h2", "h3", "h4", "h5", "h6", "head",
        "header", "hr", "html", "i", "iframe", "img",
        "input", "label", "li", "link", "main", "meta",
        "nav", "ol", "option", "p", "pre", "script",
        "section", "select", "small", "span", "strong", "style",
        "summary", "svg", "table", "tbody", "td", "template",
        "textarea", "tfoot", "th", "thead", "title", "tr",
        "u", "ul", "video",
      };
      return std::ranges::binary_search(tags, lName);
    }
  } // namespace

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
