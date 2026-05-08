#include "Internal.hpp"

namespace classifier_internal {
  ClassificationResult DetectCSS(const std::string_view s) {
    if (s.find('{') == std::string_view::npos ||
        s.find(';') == std::string_view::npos)
      return {Language::Unknown, 0.0f};

    static constexpr std::string_view props[] = {
      "color:", "background:", "margin:", "padding:",
      "border:", "display:", "position:", "font-size:",
      "font-family:", "width:", "height:", "top:",
      "left:", "right:", "bottom:", "z-index:",
      "overflow:", "text-align:", "float:", "opacity:",
      "transform:", "transition:", "animation:", "flex:",
      "grid:", "justify-content:", "align-items:",
    };
    int matches = 0;
    for (const auto prop: props) {
      if (FindCI(s, prop) != std::string_view::npos)
        matches++;
    }
    if (matches == 0)
      return {Language::Unknown, 0.0f};
    return {Language::CSS, std::min(0.6f + static_cast<float>(matches) * 0.1f, 0.95f)};
  }
} // namespace classifier_internal
