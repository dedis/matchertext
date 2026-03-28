#include "Internal.hpp"

namespace classifier_internal {

ClassificationResult DetectEmail(const std::string_view s) {
  const auto match = FindSingleEmailToken(s);
  if (match.count != 1)
    return {Language::Unknown, 0.0f};

  const auto rawPrefix = s.substr(0, match.start);
  const auto rawSuffix = s.substr(match.end);
  if (HasTightPunctuationContinuation(rawPrefix) || HasTightPunctuationContinuation(rawSuffix))
    return {Language::Unknown, 0.0f};
  if (rawPrefix.empty() && IsOnlyTerminalPunctuation(rawSuffix))
    return {Language::Unknown, 0.0f};
  if (rawSuffix.empty() && IsOnlyTerminalPunctuation(rawPrefix))
    return {Language::Unknown, 0.0f};

  const auto prefix = StripURLWrappers(rawPrefix);
  const auto suffix = StripURLWrappers(rawSuffix);
  if (!prefix.empty() && !suffix.empty() &&
      (!LooksLikeBriefURLContext(prefix) || !LooksLikeBriefURLContext(suffix)))
    return {Language::Unknown, 0.0f};

  if (!prefix.empty() && !LooksLikeBriefURLContext(prefix))
    return {Language::Unknown, 0.0f};
  if (!suffix.empty() && !LooksLikeBriefURLContext(suffix))
    return {Language::Unknown, 0.0f};

  if (prefix.empty() && suffix.empty())
    return {Language::Email, 0.95f};
  return {Language::PseudoEmail, 0.88f};
}

} // namespace classifier_internal
