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
  X(complianceRate, "Average With Toothpicks", "Average toothpick count among files that contain toothpicks.")
CREATE_STATS_HPP_NO_EXTRA(FileStats, FILE_STATS_FIELDS)

#endif //FILESTATS_HPP
