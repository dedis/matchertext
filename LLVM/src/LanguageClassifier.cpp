//
// LanguageClassifier.cpp
// Author: Antoine Bastide
// Date: 24/03/2026
//

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

#include "../include/LanguageClassifier.hpp"
#include "../include/LanguageModel.generated.hpp"
#include "classifiers/Internal.hpp"

const char *LanguageName(const Language lang) {
  static constexpr const char *names[] = {
    "Unknown", "PlainText", "URL", "FilePath", "FormatString",
    "SQL", "HTML", "XML", "JSON", "YAML",
    "CSS", "Regex", "Shell", "Python", "JavaScript",
    "TypeScript", "Java", "C", "C++", "C#",
    "Go", "Rust", "Ruby", "PHP", "Perl",
    "Lua", "Swift", "Kotlin", "R", "Scala",
    "Haskell", "OCaml", "Erlang", "Elixir", "Dart",
    "Objective-C", "GLSL", "HLSL", "IdentifierLike",
    "HexData", "BinaryData", "InlineAsm", "Email",
    "PseudoURL", "PseudoEmail", "PseudoBinaryData",
  };
  static_assert(std::size(names) == static_cast<size_t>(Language::COUNT));
  const auto idx = static_cast<size_t>(lang);
  return idx < std::size(names) ? names[idx] : "Unknown";
}

namespace {
  bool HasRepeatedCharRun(
    const std::string_view s,
    const int minRunLength = 5,
    int *maxRunLength = nullptr
  ) {
    int currentRunLength = 1;
    int maxRepeatedRun = 1;
    char repeatedChar = '\0';
    bool hasRepeatedRun = false;

    for (const char c: s) {
      if (std::isspace(static_cast<unsigned char>(c))) {
        repeatedChar = '\0';
        currentRunLength = 1;
        continue;
      }

      if (c == repeatedChar) {
        currentRunLength++;
      } else {
        repeatedChar = c;
        currentRunLength = 1;
      }

      maxRepeatedRun = std::max(maxRepeatedRun, currentRunLength);
      if (currentRunLength >= minRunLength)
        hasRepeatedRun = true;
    }

    if (maxRunLength != nullptr)
      *maxRunLength = maxRepeatedRun;
    return hasRepeatedRun;
  }

  bool IsTokenLikeSeparator(const char c) {
    return c == '_' || c == '-' || c == '.' || c == '/' || c == ':';
  }

  bool LooksLikeTokenLikeUnknown(const std::string_view s) {
    const auto trimmed = classifier_internal::Trim(s);
    if (trimmed.size() < 4 || trimmed.size() > 160)
      return false;

    if (trimmed.find_first_of(" \t\n\r") != std::string_view::npos)
      return false;
    if (classifier_internal::LooksLikeBareDomainLikeToken(trimmed))
      return false;

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
      } else if (IsTokenLikeSeparator(c)) {
        separators++;
        prevWasSeparator = true;
        if (c == '.')
          hasDot = true;
        if (c == '/')
          hasSlash = true;
        if (c == ':' && prev == ':')
          hasDoubleColon = true;
      } else {
        return false;
      }
      prev = c;
    }

    if (letters == 0 || (separators == 0 && camelTransitions == 0))
      return false;

    const float letterRatio = static_cast<float>(letters) /
                              static_cast<float>(trimmed.size());
    if (letterRatio < 0.45f)
      return false;

    const int signals = componentStarts + camelTransitions +
                        (hasDoubleColon ? 2 : 0) + (hasDot ? 1 : 0) +
                        (hasSlash ? 1 : 0);
    if (signals < 2)
      return false;

    if (digits > 0 && letters < 3)
      return false;

    return true;
  }

  uint32_t PackTrigram(const char a, const char b, const char c) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(c));
  }

  const std::array<uint16_t, 64> &ModelTrigramCounts() {
    static const std::array<uint16_t, 64> counts = [] {
      std::array<uint16_t, 64> values{};
      static_assert(kNumLanguages <= values.size(), "Increase model trigram count buffer");
      for (const auto kCombinedEntry: kCombinedEntries) {
        if (kCombinedEntry.languageIdx < values.size())
          values[kCombinedEntry.languageIdx]++;
      }
      return values;
    }();
    return counts;
  }

  ClassificationResult NaiveBayesClassify(const std::string_view body) {
    const size_t numTrigrams = body.size() >= 3 ? body.size() - 2 : 0;
    if (numTrigrams == 0)
      return {Language::Unknown, 0.0f};

    constexpr size_t kMaxLangs = 64;
    static_assert(kNumLanguages <= kMaxLangs, "Increase kMaxLangs");
    float scores[kMaxLangs]{};
    uint16_t matchedCounts[kMaxLangs]{};
    for (size_t i = 0; i < kNumLanguages; i++) {
      scores[i] = kLanguageInfos[i].logPrior +
                  static_cast<float>(numTrigrams) * kLanguageInfos[i].unseenLogProb;
    }

    for (size_t i = 0; i + 2 < body.size(); i++) {
      const uint32_t tri = PackTrigram(body[i], body[i + 1], body[i + 2]);

      size_t lo = 0;
      size_t hi = kNumCombinedEntries;
      while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (kCombinedEntries[mid].trigram < tri)
          lo = mid + 1;
        else
          hi = mid;
      }

      while (lo < kNumCombinedEntries && kCombinedEntries[lo].trigram == tri) {
        matchedCounts[kCombinedEntries[lo].languageIdx]++;
        scores[kCombinedEntries[lo].languageIdx] +=
            kCombinedEntries[lo].adjustedLogProb;
        lo++;
      }
    }

    float maxScore = -std::numeric_limits<float>::infinity();
    float secondMax = -std::numeric_limits<float>::infinity();
    size_t bestIdx = 0;
    for (size_t i = 0; i < kNumLanguages; i++) {
      if (scores[i] > maxScore) {
        secondMax = maxScore;
        maxScore = scores[i];
        bestIdx = i;
      } else if (scores[i] > secondMax) {
        secondMax = scores[i];
      }
    }

    float sumExp = 0.0f;
    for (size_t i = 0; i < kNumLanguages; i++)
      sumExp += std::exp(scores[i] - maxScore);
    const float confidence = 1.0f / sumExp;

    const uint16_t bestMatched = matchedCounts[bestIdx];
    if (bestMatched < 6)
      return {Language::Unknown, 0.0f};

    const auto &modelTrigramCounts = ModelTrigramCounts();
    const float modelSize =
        static_cast<float>(std::max<uint16_t>(modelTrigramCounts[bestIdx], 1));
    const float matchRatio =
        static_cast<float>(bestMatched) / static_cast<float>(numTrigrams);
    const float requiredMatchRatio =
        std::min(0.80f, 0.18f + 7.0f / std::sqrt(modelSize));
    if (matchRatio < requiredMatchRatio)
      return {Language::Unknown, 0.0f};

    if (const float gap = (maxScore - secondMax) / static_cast<float>(numTrigrams); gap < 0.3f)
      return {Language::Unknown, 0.0f};

    return {static_cast<Language>(kLanguageInfos[bestIdx].languageId), confidence};
  }
} // namespace

ClassificationResult ClassifyString(const std::string_view body, const float minConfidence) {
  if (body.size() < 2)
    return {Language::Unknown, 0.0f};

  const std::string normalized = classifier_internal::NormalizeForClassification(body);
  const std::string_view text = normalized.empty() ? body : std::string_view(normalized);

  if (const auto result = classifier_internal::DetectEmail(text);
    result.confidence >= minConfidence)
    return result;

  if (const auto result = classifier_internal::DetectURL(text);
    result.confidence >= minConfidence)
    return result;

  if (const auto result = classifier_internal::DetectPseudoBinaryData(body);
    result.confidence >= minConfidence)
    return result;

  if (const auto result = classifier_internal::DetectBinaryData(body);
    result.confidence >= minConfidence)
    return result;

  if (classifier_internal::LooksLikeBareDomainLikeToken(text))
    return {Language::Unknown, 0.0f};

  using Detector = ClassificationResult (*)(std::string_view);
  static constexpr Detector detectors[] = {
    classifier_internal::DetectXML,
    classifier_internal::DetectFilePath,
    classifier_internal::DetectHexData,
    classifier_internal::DetectSQL,
    classifier_internal::DetectJSON,
    classifier_internal::DetectHTML,
    classifier_internal::DetectRegex,
    classifier_internal::DetectInlineAsm,
    classifier_internal::DetectFormatString,
    classifier_internal::DetectCSS,
    classifier_internal::DetectShell,
    classifier_internal::DetectYAML,
    classifier_internal::DetectCPPDeclarationFragment,
  };

  for (const auto detect: detectors) {
    if (const auto result = detect(text); result.confidence >= minConfidence)
      return result;
  }

  if (LooksLikeTokenLikeUnknown(text)) {
    bool hasCamelTransition = false;
    for (size_t i = 1; i < text.size(); i++) {
      if (std::islower(static_cast<unsigned char>(text[i - 1])) &&
          std::isupper(static_cast<unsigned char>(text[i]))) {
        hasCamelTransition = true;
        break;
      }
    }
    if (text.find('/') == std::string_view::npos &&
        (text.find("::") != std::string_view::npos || hasCamelTransition))
      return {Language::IdentifierLike, 0.95f};
    return {Language::Unknown, 0.95f};
  }

  if (const auto result = classifier_internal::DetectPlainText(text);
    result.confidence >= minConfidence)
    return result;

  if (const auto result = classifier_internal::DetectSeparatorLine(text);
    result.confidence >= minConfidence)
    return result;

  if (HasRepeatedCharRun(text))
    return {Language::Unknown, 0.0f};

  if (text.size() >= 15) {
    const float nbThreshold = std::max(minConfidence, 0.85f);
    if (const auto result = NaiveBayesClassify(text); result.confidence >= nbThreshold) {
      if (result.language == Language::YAML &&
          !classifier_internal::HasStrongYAMLEvidence(text))
        return {Language::Unknown, 0.0f};
      return result;
    }
  }

  return {Language::Unknown, 0.0f};
}
