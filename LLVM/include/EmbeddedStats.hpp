//
// EmbeddedStats.hpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#ifndef EMBEDDED_STATS_HPP
#define EMBEDDED_STATS_HPP

#include <atomic>
#include <string>
#include <tuple>
#include <vector>

#include "AtomicString.hpp"
#include "StatsGenerator.hpp"

/// Single source of truth for stats fields
#define EMBEDDED_STATS_FIELDS(X) \
  X(count, "Sample Size", "Number of samples processed.") \
  X(withToothpicks, "With Toothpicks", "Number of samples containing at least one toothpick.") \
  X(toothpicks, "Total Toothpicks", "Total toothpick count accumulated across all samples.") \
  X(toothpicksMax, "Maximum Toothpicks", "Highest toothpick count observed in a single sample.") \
  X(toothpicksAvg, "Average Toothpicks", "Average toothpick count per sample.") \
  X(toothpicksAvgWith, "Average With Toothpicks", "Average toothpick count among samples that contain toothpicks.") \
  \
  X(scopeCount, "In-Scope Samples", "Number of samples within matchertext's scope: all samples except string literals classified as BinaryData or PseudoBinaryData. The compliance, rewrite, and nesting stats cover only these samples.") \
  X(scopeToothpicks, "In-Scope Toothpicks", "Total toothpick count of the in-scope samples.") \
  X(scopeToothpicksMax, "In-Scope Maximum Toothpicks", "Highest toothpick count observed in a single in-scope sample.") \
  \
  X(withNonCompliance, "With Non-Compliance", "Number of in-scope samples with at least one non-compliance.") \
  X(nonComplianceCount, "Non-Compliance Count", "Total number of non-compliance events across in-scope samples.") \
  X(nonComplianceMax, "Non-Compliance Max", "Highest non-compliance count observed in a single in-scope sample.") \
  X(nonComplianceAvg, "Avg Unmatched Matchers Per Sample", "Average non-compliance count per in-scope sample.") \
  X(complianceRate, "Compliance Rate", "Percentage of in-scope samples without non-compliance.") \
  \
  X(compliantCount, "Compliant Samples", "Number of in-scope samples with no matchertext non-compliance (In-Scope Samples minus With Non-Compliance).") \
  X(withToothpicksConverted, "With Toothpicks (Converted)", "Number of in-scope samples still containing at least one toothpick after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(toothpicksConverted, "Total Toothpicks (Converted)", "Total in-scope toothpick count after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(toothpicksConvertedMax, "Maximum Toothpicks (Converted)", "Highest toothpick count in a single in-scope sample after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(toothpicksConvertedAvg, "Average Toothpicks (Converted)", "Average toothpick count per in-scope sample after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(toothpicksConvertedAvgWith, "Average With Toothpicks (Converted)", "Average toothpick count among in-scope samples that still contain toothpicks after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(toothpicksReductionTotal, "Toothpick Reduction Total (%)", "Percentage reduction in in-scope toothpicks after rewriting matchertext-compliant string literals as C++ raw string literals.") \
  X(toothpicksReductionAvg, "Toothpick Reduction Average (%)", "Mean of each in-scope sample's own percentage reduction in toothpicks after rewriting matchertext-compliant string literals as C++ raw string literals (samples without toothpicks count as 0%).") \
  X(toothpicksReductionMax, "Toothpick Reduction Maximum (%)", "Percentage reduction in the maximum per-sample in-scope toothpick count after rewriting matchertext-compliant string literals as C++ raw string literals.") \
  X(keptNonCompliant, "Kept Toothpicks: Non-Compliant", "Converted toothpicks of in-scope string literals that are not matchertext-compliant, so not rewritten: all their toothpicks.") \
  X(keptNotMatchertext, "Kept Toothpicks: Not Matchertext Decoded", "Converted toothpicks of compliant string literals whose decoded content is not matchertext (\\x5B is [), so not rewritten: all their toothpicks.") \
  X(keptRaw, "Kept Toothpicks: Already Raw", "Converted toothpicks that are literal backslashes of an already raw literal or segment, such as R\"(\\d)\" or r'\\d'.") \
  X(keptUnknownEscape, "Kept Toothpicks: Unknown Escape", "Converted toothpicks of unknown escapes the language keeps as written, backslash included (PHP, OCaml \\d).") \
  X(keptInterpolation, "Kept Toothpicks: Interpolation", "Converted toothpicks that open an interpolation, such as Swift's \\(name), which a raw literal writes as \\#(name).") \
  X(keptBackslash, "Kept Toothpicks: Escaped Backslash", "Converted toothpicks of escapes that decode to a backslash, such as \\\\ in a regular expression.") \
  X(keptUnwritable, "Kept Toothpicks: Not Writable Raw", "Converted toothpicks of escapes a raw literal cannot write, such as a byte that is not valid UTF-8 or a lone surrogate.") \
  X(keptControl, "Kept Toothpicks: Control Character", "Converted toothpicks of escapes of control characters other than tab, line feed, and carriage return, kept verbatim.") \
  X(rawHoldable, "Raw-Form Holdable", "Number of matchertext-compliant string literals whose content the language's default raw string form holds verbatim.") \
  X(matchertextOnly, "Matchertext-Only", "Number of matchertext-compliant string literals whose content a matchertext literal M\"(...)\" holds verbatim but the language's default raw string form does not.") \
  X(scopeWithToothpicks, "In-Scope With Toothpicks", "Number of in-scope samples containing at least one toothpick.") \
  X(compliantWithToothpicks, "Compliant With Toothpicks", "Number of in-scope samples with no matchertext non-compliance and at least one toothpick.") \
  X(clearedWithToothpicks, "Cleared With Toothpicks", "Number of in-scope samples with at least one toothpick and none left after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(rawHoldableWithToothpicks, "Raw-Form Holdable With Toothpicks", "Raw-Form Holdable string literals with at least one toothpick.") \
  X(matchertextOnlyWithToothpicks, "Matchertext-Only With Toothpicks", "Matchertext-Only string literals with at least one toothpick.") \
  X(toothpicksReductionSumWith, "Sum Of Per-Sample Toothpick Reduction (%)", "Sum of each in-scope sample's own percentage reduction in toothpicks after rewriting matchertext-compliant string literals as C++ raw string literals, over the samples with at least one toothpick.") \
  X(toothpicksReductionAvgWith, "Toothpick Reduction Average With Toothpicks (%)", "Mean of each in-scope sample's own percentage reduction in toothpicks after rewriting matchertext-compliant string literals as C++ raw string literals, over the samples with at least one toothpick.") \
  \
  X(withNesting, "With Raw Nested Embedding", "Number of in-scope samples whose raw nesting depth exceeds 1, even if the nesting is never closed.") \
  X(nestingDepthTotal, "Sum Of Per-Sample Raw Max Depth", "Sum of each in-scope sample's maximum raw nesting depth, counting unmatched openers such as '((('.") \
  X(nestingDepthMax, "Highest Per-Sample Raw Max Depth", "Greatest raw nesting depth observed in any single in-scope sample, even if the nesting is left open.") \
  X(nestingDepthAvg, "Avg Per-Sample Raw Max Depth", "Average of the maximum raw nesting depth measured per in-scope sample.") \
  X(withValidNesting, "With Valid Nested Embedding", "Number of in-scope samples whose nesting depth exceeds 1 and is confirmed by matching closers, such as '((()))'.") \
  X(validNestingDepthTotal, "Sum Of Per-Sample Valid Max Depth", "Sum of each in-scope sample's maximum valid nesting depth: the matched pairs around a matched pair, itself included. Unmatched openers add no level.") \
  X(validNestingDepthMax, "Highest Per-Sample Valid Max Depth", "Greatest valid nesting depth observed in any single in-scope sample. Unmatched openers add no level.") \
  X(validNestingDepthAvg, "Avg Per-Sample Valid Max Depth", "Average of the maximum valid nesting depth measured per in-scope sample.") \
  \
  X(rawChars, "Raw Character Count", "Total number of raw input characters processed.")

#define EMBEDDED_EXTRA_FIELDS \
  AtomicString stringMaxToothpicks; \
  AtomicString stringMaxNonCompliance; \
  AtomicString stringMaxNested; \
  AtomicString stringMaxValidNested; \
  /* Running sum of per-sample toothpick reduction percentages; consumed by */ \
  /* DeriveStats to compute toothpicksReductionAvg. Not an output column. */ \
  std::atomic<double> toothpicksReductionSum{0};

CREATE_STATS_HPP(EmbeddedStats, EMBEDDED_STATS_FIELDS, EMBEDDED_EXTRA_FIELDS
)

#endif // EMBEDDED_STATS_HPP
