#include "Internal.hpp"

namespace classifier_internal {
  ClassificationResult DetectRegex(const std::string_view s) {
    if (s.size() < 3)
      return {Language::Unknown, 0.0f};

    int signals = 0;

    static constexpr std::string_view escapes[] = {
      "\\d", "\\D", "\\w", "\\W", "\\s", "\\S", "\\b", "\\B",
    };
    for (const auto esc: escapes) {
      if (s.find(esc) != std::string_view::npos)
        signals += 2;
    }

    static constexpr std::string_view groups[] = {
      "(?:", "(?=", "(?!", "(?<=", "(?<!", "(?P<", "(?P=",
    };
    for (const auto grp: groups) {
      if (s.find(grp) != std::string_view::npos)
        signals += 3;
    }

    if (s.find('[') != std::string_view::npos &&
        s.find(']') != std::string_view::npos)
      signals++;

    for (size_t i = 0; i + 2 < s.size(); i++) {
      if (s[i] == '{' && std::isdigit(static_cast<unsigned char>(s[i + 1]))) {
        size_t j = i + 1;
        while (j < s.size() &&
               (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == ','))
          j++;
        if (j < s.size() && s[j] == '}')
          signals += 2;
      }
    }

    if (s.front() == '^')
      signals++;
    if (s.back() == '$')
      signals++;

    if (signals < 2)
      return {Language::Unknown, 0.0f};
    return {Language::Regex, std::min(0.5f + static_cast<float>(signals) * 0.08f, 0.95f)};
  }
} // namespace classifier_internal
