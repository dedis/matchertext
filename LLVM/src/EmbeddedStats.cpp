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
  // Compliance, rewrite and nesting stats cover only in-scope samples.
  const double sn = scopeCount.load(std::memory_order_relaxed);
  const double stp = scopeToothpicks.load(std::memory_order_relaxed);
  const double vnd = validNestingDepthTotal.load(std::memory_order_relaxed);

  toothpicksAvg.store(n > 0.0 ? tp / n : 0.0, std::memory_order_relaxed);
  toothpicksAvgWith.store(wn > 0.0 ? tp / wn : 0.0, std::memory_order_relaxed);
  nonComplianceAvg.store(sn > 0.0 ? nc / sn : 0.0, std::memory_order_relaxed);
  complianceRate.store(100.0 * (sn > 0.0 ? (sn - wnc) / sn : 0.0), std::memory_order_relaxed);
  nestingDepthAvg.store(sn > 0.0 ? nd / sn : 0.0, std::memory_order_relaxed);
  validNestingDepthAvg.store(sn > 0.0 ? vnd / sn : 0.0, std::memory_order_relaxed);

  // Stats after rewriting every matchertext-compliant string literal as an
  // equivalent C++ raw string literal (non-compliant strings and comments are
  // counted unchanged, so for non-string sources the reductions come out as 0).
  const double tpc = toothpicksConverted.load(std::memory_order_relaxed);
  const double wnc2 = withToothpicksConverted.load(std::memory_order_relaxed);
  const double tpcMax = toothpicksConvertedMax.load(std::memory_order_relaxed);
  const double tpcAvg = sn > 0.0 ? tpc / sn : 0.0;
  const double tpMax = scopeToothpicksMax.load(std::memory_order_relaxed);
  const double trs = toothpicksReductionSum.load(std::memory_order_relaxed);

  compliantCount.store(sn - wnc, std::memory_order_relaxed);
  toothpicksConvertedAvg.store(tpcAvg, std::memory_order_relaxed);
  toothpicksConvertedAvgWith.store(wnc2 > 0.0 ? tpc / wnc2 : 0.0, std::memory_order_relaxed);
  toothpicksReductionTotal.store(stp > 0.0 ? 100.0 * (stp - tpc) / stp : 0.0, std::memory_order_relaxed);
  // Mean of per-sample reductions. Aggregating per-sample percentages this way is
  // genuinely distinct from the aggregate total: (tp-tpc)/tp weights each sample by
  // its toothpick count, whereas this weights every sample equally.
  toothpicksReductionAvg.store(sn > 0.0 ? trs / sn : 0.0, std::memory_order_relaxed);
  const double swn = scopeWithToothpicks.load(std::memory_order_relaxed);
  toothpicksReductionSumWith.store(trs, std::memory_order_relaxed);
  toothpicksReductionAvgWith.store(swn > 0.0 ? trs / swn : 0.0, std::memory_order_relaxed);
  toothpicksReductionMax.store(tpMax > 0.0 ? 100.0 * (tpMax - tpcMax) / tpMax : 0.0, std::memory_order_relaxed);
}
