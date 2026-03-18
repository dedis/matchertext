//
// EmbeddedStats.cpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#include "../include/EmbeddedStats.hpp"

CREATE_STATS_CPP(EmbeddedStats, EMBEDDED_STATS_FIELDS)

void EmbeddedStats::DeriveStats() {
  const double n = count.load(std::memory_order_relaxed);
  const double wn = withToothpicks.load(std::memory_order_relaxed);
  const double nc = nonComplianceCount.load(std::memory_order_relaxed);
  const double wnc = withNonCompliance.load(std::memory_order_relaxed);
  const double tp = toothpicks.load(std::memory_order_relaxed);
  const double nd = nestingDepthTotal.load(std::memory_order_relaxed);
  const double vnd = validNestingDepthTotal.load(std::memory_order_relaxed);

  toothpicksAvg.store(n > 0.0 ? tp / n : 0.0, std::memory_order_relaxed);
  toothpicksAvgWith.store(wn > 0.0 ? tp / wn : 0.0, std::memory_order_relaxed);
  nonComplianceAvg.store(n > 0.0 ? nc / n : 0.0, std::memory_order_relaxed);
  complianceRate.store(100.0 * (n > 0.0 ? (n - wnc) / n : 0.0), std::memory_order_relaxed);
  nestingDepthAvg.store(n > 0.0 ? nd / n : 0.0, std::memory_order_relaxed);
  validNestingDepthAvg.store(n > 0.0 ? vnd / n : 0.0, std::memory_order_relaxed);
}
