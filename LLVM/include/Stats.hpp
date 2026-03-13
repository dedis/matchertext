//
// Stats.hpp
// Author: Antoine Bastide
// Date: 09.03.2026
//

#ifndef STATS_HPP
#define STATS_HPP

#include <atomic>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "AtomicString.hpp"

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

/// Runtime stats (atomics for concurrent updates)
struct EmbeddedStats {
  #define DECLARE_FIELD(name, label, ...) std::atomic<double> name{0};
  EMBEDDED_STATS_FIELDS(DECLARE_FIELD)
  #undef DECLARE_FIELD

  AtomicString stringMaxToothpicks;
  AtomicString stringMaxNonCompliance;
  AtomicString stringMaxNested;
  AtomicString stringMaxValidNested;

  /// Some stats can't be set during parsing because they need to use global stats set by the parser.
  /// So this function runs after the global parsing pass and creates them, ex: toothpicksAvg
  void DeriveStats();
};

/// Immutable snapshot used for printing
struct EmbeddedStatsSnapshot {
  #define DECLARE_FIELD(name, label, ...) double name{};
  EMBEDDED_STATS_FIELDS(DECLARE_FIELD)
  #undef DECLARE_FIELD
};

struct NestedStats {
  void Record(uint64_t rawDepth, uint64_t validDepth);
  private:
    mutable std::mutex mutex_;
    std::vector<uint64_t> rawLevels_;
    std::vector<uint64_t> validLevels_;

    static void RecordLevel(std::vector<uint64_t> &levels, uint64_t depth);

    friend struct NestedStatsSnapshot;
    friend NestedStatsSnapshot SnapshotNestedStats(const NestedStats &stats);
};

struct NestedStatsSnapshot {
  std::vector<uint64_t> rawLevels;
  std::vector<uint64_t> validLevels;
};

EmbeddedStatsSnapshot SnapshotStats(const EmbeddedStats &stats);
NestedStatsSnapshot SnapshotNestedStats(const NestedStats &stats);

/// Logging methods to display the stats nicely
std::vector<std::tuple<std::string, double, std::string>> ToColumns(const EmbeddedStatsSnapshot &snapshot);
void PrintStatsTable(const std::vector<std::pair<std::string, EmbeddedStatsSnapshot>> &rows);
void PrintNestedStatsTable(const std::vector<std::pair<std::string, NestedStatsSnapshot>> &rows);
void PrintStatsMaxString(const EmbeddedStats &strings, const EmbeddedStats &docs);

#endif //STATS_HPP
