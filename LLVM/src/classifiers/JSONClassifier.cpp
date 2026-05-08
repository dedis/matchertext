#include "Internal.hpp"

namespace classifier_internal {
  ClassificationResult DetectJSON(const std::string_view s) {
    const auto trimmed = TrimLeft(s);
    if (trimmed.empty())
      return {Language::Unknown, 0.0f};
    if (trimmed[0] != '{' && trimmed[0] != '[')
      return {Language::Unknown, 0.0f};

    auto countPairs = [&](const char quote) {
      int pairs = 0;
      for (size_t i = 0; i + 2 < s.size(); i++) {
        if (s[i] != quote)
          continue;
        const size_t close = s.find(quote, i + 1);
        if (close == std::string_view::npos)
          break;
        const size_t next = s.find_first_not_of(" \t\n\r", close + 1);
        if (next != std::string_view::npos && s[next] == ':') {
          pairs++;
          i = next;
        } else {
          i = close;
        }
      }
      return pairs;
    };

    const int doubleQuotedPairs = countPairs('"');
    if (doubleQuotedPairs > 0)
      return {
        Language::JSON,
        std::min(0.6f + static_cast<float>(doubleQuotedPairs) * 0.1f, 0.95f)
      };

    const int singleQuotedPairs = countPairs('\'');
    if (singleQuotedPairs == 0)
      return {Language::Unknown, 0.0f};
    return {
      Language::JSON,
      std::min(0.68f + static_cast<float>(singleQuotedPairs) * 0.08f, 0.88f)
    };
  }
} // namespace classifier_internal
