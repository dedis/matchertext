#include "Internal.hpp"

namespace classifier_internal {
  ClassificationResult DetectInlineAsm(const std::string_view s) {
    if (s.size() < 4)
      return {Language::Unknown, 0.0f};

    int signals = 0;

    static constexpr std::string_view directives[] = {
      ".align", ".ascii", ".asciz", ".balign", ".byte", ".fill",
      ".globl", ".inst", ".long", ".macro", ".octa", ".popsection",
      ".previous", ".pushsection", ".quad", ".section", ".short",
      ".size", ".type", ".word",
    };
    for (const auto directive: directives) {
      if (FindCI(s, directive) != std::string_view::npos)
        signals += 2;
    }

    for (size_t i = 0; i + 1 < s.size(); i++) {
      if (s[i] != '%')
        continue;

      if (s[i + 1] == '[') {
        const size_t close = s.find(']', i + 2);
        if (close != std::string_view::npos && close > i + 2)
          signals += 2;
        continue;
      }

      if (!std::isdigit(static_cast<unsigned char>(s[i + 1])))
        continue;

      size_t j = i + 1;
      while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
        j++;
      if (j >= s.size()) {
        signals += 2;
        continue;
      }

      const char next = s[j];
      if (next == ',' || std::isspace(static_cast<unsigned char>(next)) ||
          next == ')' || next == '(' || next == ']' || next == '"' ||
          next == ';' || next == ':') {
        signals += 2;
      }
    }

    if (FindCI(s, "\\n.") != std::string_view::npos ||
        FindCI(s, "\n.") != std::string_view::npos)
      signals++;

    if (signals < 2)
      return {Language::Unknown, 0.0f};

    return {
      Language::InlineAsm,
      std::min(0.70f + static_cast<float>(signals) * 0.05f, 0.97f)
    };
  }
} // namespace classifier_internal
