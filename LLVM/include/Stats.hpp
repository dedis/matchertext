//
// Stats.hpp
// Author: Antoine Bastide
// Date: 09.03.2026
//

#ifndef STATS_HPP
#define STATS_HPP

#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "AtomicString.hpp"

/// Single source of truth for stats fields
#define EMBEDDED_STATS_FIELDS(X)                          \
  X(count, "Sample Size")                                 \
  X(withToothpicks, "With Toothpicks")                    \
  X(toothpicks, "Total Toothpicks")                       \
  X(toothpicksMax, "Maximum Toothpicks")                  \
  X(toothpicksAvg, "Average Toothpicks")                  \
  X(toothpicksAvgWith, "Average With Toothpicks")         \
                                                          \
  X(withNonCompliance, "With Non-Compliance")             \
  X(nonComplianceCount, "Non-Compliance Count")           \
  X(nonComplianceMax, "Non-Compliance Max")               \
  X(nonComplianceAvg, "Avg Unmatched Matchers Per Sample") \
  X(complianceRate, "Compliance Rate")                    \
                                                          \
  X(withNesting, "With Nested Embedding")                 \
  X(nestingDepthTotal, "Sum Of Per-Sample Max Depth")     \
  X(nestingDepthMax, "Highest Per-Sample Max Depth")      \
  X(nestingDepthAvg, "Avg Per-Sample Max Depth")          \
                                                          \
  X(rawChars, "Raw Character Count")

/// Runtime stats (atomics for concurrent updates)
struct EmbeddedStats {
  #define DECLARE_FIELD(name, label) std::atomic<double> name{0};
  EMBEDDED_STATS_FIELDS(DECLARE_FIELD)
  #undef DECLARE_FIELD

  AtomicString stringMaxToothpicks;
  AtomicString stringMaxNonCompliance;
  AtomicString stringMaxNested;

  /// Some stats can't be set during parsing because they need to use global stats set by the parser.
  /// So this function runs after the global parsing pass and creates them, ex: toothpicksAvg
  void DeriveStats();
};

/// Immutable snapshot used for printing
struct EmbeddedStatsSnapshot {
  #define DECLARE_FIELD(name, label) double name{};
  EMBEDDED_STATS_FIELDS(DECLARE_FIELD)
  #undef DECLARE_FIELD
};

EmbeddedStatsSnapshot SnapshotStats(const EmbeddedStats &stats);

/// Logging methods to display the stats nicely
std::vector<std::pair<std::string, double>> ToColumns(const EmbeddedStatsSnapshot &snapshot);
void PrintStatsTable(const std::vector<std::pair<std::string, EmbeddedStatsSnapshot>> &rows);
void PrintStatsMaxString(const EmbeddedStats &strings, const EmbeddedStats &docs);

#endif //STATS_HPP
