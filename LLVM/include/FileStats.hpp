//
// FileStats.hpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#ifndef FILESTATS_HPP
#define FILESTATS_HPP

#include <string>
#include <vector>

#include "StatsGenerator.hpp"

#define FILE_STATS_FIELDS(X) \
  X(count, "Sample Size", "Number of files processed.") \
  X(withViolation, "With Violation", "Number of files containing at least one MatcherText violation.") \
  X(violationCount, "Total Violations", "Total violation count accumulated across all files.") \
  X(violationMax, "Maximum Violations", "Highest violation count observed in a single file.") \
  X(violationAvg, "Average Violations", "Average violation count per file.") \
  X(complianceRate, "Compliance Rate", "Percentage of files without non-compliance.") \
  X(withViolationRelaxed, "With Violation Relaxed", "Number of files containing at least one relaxed MatcherText violation.") \
  X(violationCountRelaxed, "Total Relaxed Violations", "Total relaxed violation count accumulated across all files.") \
  X(violationMaxRelaxed, "Maximum Relaxed Violations", "Highest relaxed violation count observed in a single file.") \
  X(violationAvgRelaxed, "Average Relaxed Violations", "Average relaxed violation count per file.") \
  X(complianceRateRelaxed, "Compliance Rate Relaxed", "Percentage of files without relaxed non-compliance.") \
  X(skippedOversized, "Skipped Oversized", "Files not parsed because they exceed 16 MiB; not in Sample Size.") \
  X(skippedBinary, "Skipped Binary", "Files not parsed because they contain a NUL byte; not in Sample Size.") \
  X(skippedTimeout, "Skipped Timeout", "Files whose parse did not finish within 5 seconds; not in Sample Size.")
CREATE_STATS_HPP_NO_EXTRA(FileStats, FILE_STATS_FIELDS)

/// Why a file yielded no samples without being parsed.
enum class SkipReason { None, Oversized, Binary, Timeout };

/// Counts a skipped file in `fs`.
void RecordSkip(FileStats &fs, SkipReason reason);

#endif //FILESTATS_HPP
