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
  X(withNonCompliance, "With Non-Compliance", "Number of samples with at least one non-compliance.") \
  X(nonComplianceCount, "Non-Compliance Count", "Total number of non-compliance events across all samples.") \
  X(nonComplianceMax, "Non-Compliance Max", "Highest non-compliance count observed in a single sample.") \
  X(nonComplianceAvg, "Avg Unmatched Matchers Per Sample", "Average non-compliance count per sample.") \
  X(complianceRate, "Compliance Rate", "Percentage of samples without non-compliance.") \
  \
  X(compliantCount, "Compliant Samples", "Number of samples with no matchertext non-compliance (count minus With Non-Compliance).") \
  X(withToothpicksConverted, "With Toothpicks (Converted)", "Number of samples still containing at least one toothpick after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(toothpicksConverted, "Total Toothpicks (Converted)", "Total toothpick count after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(toothpicksConvertedMax, "Maximum Toothpicks (Converted)", "Highest toothpick count in a single sample after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(toothpicksConvertedAvg, "Average Toothpicks (Converted)", "Average toothpick count per sample after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(toothpicksConvertedAvgWith, "Average With Toothpicks (Converted)", "Average toothpick count among samples that still contain toothpicks after rewriting each matchertext-compliant string literal as an equivalent C++ raw string literal.") \
  X(toothpicksReductionTotal, "Toothpick Reduction Total (%)", "Percentage reduction in total toothpicks after rewriting matchertext-compliant string literals as C++ raw string literals.") \
  X(toothpicksReductionAvg, "Toothpick Reduction Average (%)", "Percentage reduction in average per-sample toothpicks after rewriting matchertext-compliant string literals as C++ raw string literals.") \
  X(toothpicksReductionMax, "Toothpick Reduction Maximum (%)", "Percentage reduction in the maximum per-sample toothpick count after rewriting matchertext-compliant string literals as C++ raw string literals.") \
  \
  X(withNesting, "With Raw Nested Embedding", "Number of samples whose raw nesting depth exceeds 1, even if the nesting is never closed.") \
  X(nestingDepthTotal, "Sum Of Per-Sample Raw Max Depth", "Sum of each sample's maximum raw nesting depth, counting unmatched openers such as '((('.") \
  X(nestingDepthMax, "Highest Per-Sample Raw Max Depth", "Greatest raw nesting depth observed in any single sample, even if the nesting is left open.") \
  X(nestingDepthAvg, "Avg Per-Sample Raw Max Depth", "Average of the maximum raw nesting depth measured per sample.") \
  X(withValidNesting, "With Valid Nested Embedding", "Number of samples whose nesting depth exceeds 1 and is confirmed by matching closers, such as '((()))'.") \
  X(validNestingDepthTotal, "Sum Of Per-Sample Valid Max Depth", "Sum of each sample's maximum valid nesting depth, counting only depths closed by matching closers.") \
  X(validNestingDepthMax, "Highest Per-Sample Valid Max Depth", "Greatest valid nesting depth observed in any single sample, confirmed by matching closers.") \
  X(validNestingDepthAvg, "Avg Per-Sample Valid Max Depth", "Average of the maximum valid nesting depth measured per sample.") \
  \
  X(rawChars, "Raw Character Count", "Total number of raw input characters processed.")

#define EMBEDDED_EXTRA_FIELDS \
  AtomicString stringMaxToothpicks; \
  AtomicString stringMaxNonCompliance; \
  AtomicString stringMaxNested; \
  AtomicString stringMaxValidNested;

CREATE_STATS_HPP(EmbeddedStats, EMBEDDED_STATS_FIELDS, EMBEDDED_EXTRA_FIELDS
)

#endif // EMBEDDED_STATS_HPP
