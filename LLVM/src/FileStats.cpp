//
// FileStats.cpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#include "../include/FileStats.hpp"

CREATE_STATS_CPP(FileStats, FILE_STATS_FIELDS)

void FileStats::DeriveStats() {
  const double n = count.load(std::memory_order_relaxed);
  const double v = violationCount.load(std::memory_order_relaxed);
  const double wv = withViolation.load(std::memory_order_relaxed);
  const double vr = violationCountRelaxed.load(std::memory_order_relaxed);
  const double wvr = withViolationRelaxed.load(std::memory_order_relaxed);

  violationAvg.store(n > 0.0 ? v / n : 0.0, std::memory_order_relaxed);
  complianceRate.store(100.0 * (n > 0.0 ? (n - wv) / n : 0.0), std::memory_order_relaxed);
  violationAvgRelaxed.store(n > 0.0 ? vr / n : 0.0, std::memory_order_relaxed);
  complianceRateRelaxed.store(100.0 * (n > 0.0 ? (n - wvr) / n : 0.0), std::memory_order_relaxed);
}
