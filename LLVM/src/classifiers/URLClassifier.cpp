#include "Internal.hpp"

namespace classifier_internal {

ClassificationResult DetectURL(const std::string_view s) {
  const auto match = FindSingleURLToken(s);
  if (match.count != 1)
    return {Language::Unknown, 0.0f};

  const auto prefix = StripURLWrappers(s.substr(0, match.start));
  const auto suffix = StripURLWrappers(s.substr(match.end));
  if (!prefix.empty() && !suffix.empty() &&
      (!LooksLikeBriefURLContext(prefix) || !LooksLikeBriefURLContext(suffix)))
    return {Language::Unknown, 0.0f};

  if (!prefix.empty() && !LooksLikeBriefURLContext(prefix))
    return {Language::Unknown, 0.0f};
  if (!suffix.empty() && !LooksLikeBriefURLContext(suffix))
    return {Language::Unknown, 0.0f};

  if (prefix.empty() && suffix.empty())
    return {Language::URL, 0.95f};
  return {Language::PseudoURL, 0.88f};
}

} // namespace classifier_internal
