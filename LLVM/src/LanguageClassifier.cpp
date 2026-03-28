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
    "PseudoURL", "PseudoEmail",
  };
  static_assert(std::size(names) == static_cast<size_t>(Language::COUNT));
  const auto idx = static_cast<size_t>(lang);
  return idx < std::size(names) ? names[idx] : "Unknown";
}

namespace {
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
    float scores[kMaxLangs];
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

  if (const auto result = classifier_internal::DetectBinaryData(body);
    result.confidence >= minConfidence)
    return result;

  const std::string normalized = classifier_internal::NormalizeForClassification(body);
  const std::string_view text = normalized.empty() ? body : std::string_view(normalized);
  if (classifier_internal::LooksLikeBareDomainLikeToken(text))
    return {Language::Unknown, 0.0f};

  using Detector = ClassificationResult (*)(std::string_view);
  static constexpr Detector detectors[] = {
    classifier_internal::DetectEmail,
    classifier_internal::DetectURL,
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
    classifier_internal::DetectIdentifierLike,
    classifier_internal::DetectPlainText,
    classifier_internal::DetectSeparatorLine,
  };

  for (const auto detect: detectors) {
    if (const auto result = detect(text); result.confidence >= minConfidence)
      return result;
  }

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
