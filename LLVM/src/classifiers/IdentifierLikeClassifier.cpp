#include "Internal.hpp"

namespace classifier_internal {

ClassificationResult DetectIdentifierLike(const std::string_view s) {
  const auto trimmed = Trim(s);
  if (trimmed.size() < 4 || trimmed.size() > 160)
    return {Language::Unknown, 0.0f};

  if (trimmed.find_first_of(" \t\n\r") != std::string_view::npos)
    return {Language::Unknown, 0.0f};
  if (LooksLikeBareDomainLikeToken(trimmed))
    return {Language::Unknown, 0.0f};

  int letters = 0;
  int digits = 0;
  int separators = 0;
  int camelTransitions = 0;
  int componentStarts = 0;
  bool hasDoubleColon = false;
  bool hasDot = false;
  bool hasSlash = false;

  char prev = '\0';
  bool prevWasSeparator = true;
  for (const char c: trimmed) {
    if (const auto uc = static_cast<unsigned char>(c); std::isalnum(uc)) {
      if (std::isalpha(uc))
        letters++;
      if (std::isdigit(uc))
        digits++;
      if (prevWasSeparator)
        componentStarts++;
      if (std::islower(static_cast<unsigned char>(prev)) &&
          std::isupper(uc))
        camelTransitions++;
      prevWasSeparator = false;
    } else if (IsIdentifierLikeSeparator(c)) {
      separators++;
      prevWasSeparator = true;
      if (c == '.')
        hasDot = true;
      if (c == '/')
        hasSlash = true;
      if (c == ':' && prev == ':')
        hasDoubleColon = true;
    } else {
      return {Language::Unknown, 0.0f};
    }
    prev = c;
  }

  if (letters == 0 || (separators == 0 && camelTransitions == 0))
    return {Language::Unknown, 0.0f};

  const float letterRatio = static_cast<float>(letters) / static_cast<float>(trimmed.size());
  if (letterRatio < 0.45f)
    return {Language::Unknown, 0.0f};

  const int signals = componentStarts + camelTransitions +
                      (hasDoubleColon ? 2 : 0) + (hasDot ? 1 : 0) + (hasSlash ? 1 : 0);
  if (signals < 2)
    return {Language::Unknown, 0.0f};

  if (digits > 0 && letters < 3)
    return {Language::Unknown, 0.0f};

  return {
    Language::IdentifierLike,
    std::min(0.62f + static_cast<float>(signals) * 0.05f, 0.95f)
  };
}

} // namespace classifier_internal
