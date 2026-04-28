//
// LanguageClassifier.hpp
// Author: Antoine Bastide
// Date: 24/03/2026
//

#ifndef LANGUAGE_CLASSIFIER_HPP
#define LANGUAGE_CLASSIFIER_HPP

#include <cstdint>
#include <string_view>

/// Detectable categories for embedded string classification.
///
/// The numeric values are part of the generated model interface:
/// `train/generate_model.py` writes these ids into
/// `include/LanguageModel.generated.hpp`. Keep existing values stable, and
/// regenerate the model whenever the trainable language set changes.
enum class Language : uint8_t {
  Unknown = 0,
  PlainText,
  // Structural patterns (Layer 1)
  URL,
  FilePath,
  FormatString,
  // Domain-specific languages
  SQL,
  HTML,
  XML,
  JSON,
  YAML,
  CSS,
  Regex,
  Shell,
  // Programming languages
  Python,
  JavaScript,
  TypeScript,
  Java,
  C,
  CPP,
  CSharp,
  Go,
  Rust,
  Ruby,
  PHP,
  Perl,
  Lua,
  Swift,
  Kotlin,
  R,
  Scala,
  Haskell,
  OCaml,
  Erlang,
  Elixir,
  Dart,
  Objective_C,
  GLSL,
  HLSL,
  // Auxiliary string buckets
  IdentifierLike,
  HexData,
  BinaryData,
  InlineAsm,
  Email,
  PseudoURL,
  PseudoEmail,
  PseudoBinaryData,
  COUNT
};

/// Return a stable display name used in logs, metrics, and tests.
///
/// The returned pointer refers to static storage owned by the classifier.
const char *LanguageName(Language lang);

/// Result of classifying one extracted string fragment.
struct ClassificationResult {
  /// Most likely category, or `Language::Unknown` if nothing is accepted or
  /// if the string is confidently classified as a non-language separator.
  Language language;
  /// Confidence in `[0.0, 1.0]`; `0.0` means the candidate was rejected.
  /// Non-zero `Language::Unknown` values are internal suppression signals and
  /// are omitted from language statistics and debug language logs.
  float confidence;
};

/// Classify a string fragment into its most likely source language.
///
/// Intended input is the string body without surrounding quotes. The runtime
/// also strips common comment decorators and unescapes simple C-style escape
/// sequences before scoring so extracted literals can be passed directly.
///
/// Classification uses two layers:
///   Layer 1: Deterministic structural detectors for high-precision patterns
///            (URLs, file paths, JSON, HTML, SQL, regex, etc.)
///   Layer 2: Naive Bayes trigram classifier for broad language coverage
///            (programming languages and domain-specific languages).
///
/// Layer 2 depends on `include/LanguageModel.generated.hpp`, which is produced
/// by `train/generate_model.py`. The trigram classifier is only consulted for
/// sufficiently long strings and still rejects ambiguous matches, so returning
/// `Language::Unknown` is the expected fallback for low-signal inputs.
///
/// @param body The extracted string content (without surrounding quotes).
/// @param minConfidence Minimum threshold for structural detectors. The trigram
///        classifier also enforces its own stricter acceptance floor.
/// @return The detected language and confidence score.
ClassificationResult ClassifyString(std::string_view body, float minConfidence = 0.5f);

#endif // LANGUAGE_CLASSIFIER_HPP
