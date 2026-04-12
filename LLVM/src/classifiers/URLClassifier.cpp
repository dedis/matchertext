#include "Internal.hpp"

namespace classifier_internal {

ClassificationResult DetectURL(const std::string_view s) {
  const auto matches = FindURLTokens(s);
  if (matches.empty())
    return {Language::Unknown, 0.0f};

  const auto &first = matches.front();
  const auto prefix = s.substr(0, first.start);
  const auto suffix = s.substr(first.end);
  if (matches.size() == 1 && prefix.empty() && suffix.empty())
    return {Language::URL, 0.95f};

  return {Language::PseudoURL, matches.size() > 1 ? 0.92f : 0.88f};
}

} // namespace classifier_internal
