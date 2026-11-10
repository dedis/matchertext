//
// LanguageStats.cpp
// Author: Antoine Bastide
// Date: 24/03/2026
//

#include <algorithm>

#include "../include/LanguageStats.hpp"

void LanguageStats::Record(
  const LanguageEnum lang, const uint64_t violations,
  const uint64_t toothpicks
) {
  const auto idx = static_cast<size_t>(lang);
  entries_[idx].count.fetch_add(1, std::memory_order_relaxed);
  entries_[idx].violations.fetch_add(violations, std::memory_order_relaxed);
  entries_[idx].toothpicks.fetch_add(toothpicks, std::memory_order_relaxed);
}

LanguageStatsSnapshot LanguageStats::Snapshot() const {
  LanguageStatsSnapshot snap{};

  for (size_t i = 0; i < static_cast<size_t>(LanguageEnum::COUNT); i++) {
    const uint64_t c = entries_[i].count.load(std::memory_order_relaxed);
    if (c == 0)
      continue;
    snap.total += c;
    snap.entries.push_back(
      {
        static_cast<LanguageEnum>(i), c,
        entries_[i].violations.load(std::memory_order_relaxed),
        entries_[i].toothpicks.load(std::memory_order_relaxed), 0.0,
      }
    );
  }

  // Compute percentages and sort by count descending
  for (auto &e: snap.entries)
    e.percentage = snap.total > 0
                     ? static_cast<double>(e.count) / static_cast<double>(snap.total) * 100.0
                     : 0.0;

  std::ranges::sort(
    snap.entries,
    [](const LanguageStatsEntry &a, const LanguageStatsEntry &b) {
      return a.count > b.count;
    }
  );

  return snap;
}

void RawFormStats::Counts::Record(
  const uint64_t violations, const uint64_t holding, const bool ownHolds, const bool hasControl
) {
  strings.fetch_add(1, std::memory_order_relaxed);
  if (hasControl)
    control.fetch_add(1, std::memory_order_relaxed);
  if (ownHolds)
    ownForm.fetch_add(1, std::memory_order_relaxed);
  if (holding != 0)
    anyForm.fetch_add(1, std::memory_order_relaxed);
  for (size_t i = 0; i < kMaxForms; ++i)
    if (holding >> i & 1)
      forms[i].fetch_add(1, std::memory_order_relaxed);
  for (size_t i = 0; i < kMaxChecks; ++i)
    if (violations >> i & 1)
      checks[i].fetch_add(1, std::memory_order_relaxed);
}

void RawFormStats::Record(
  const bool isInScope, const bool isMatchertext, const uint64_t violations, const uint64_t holding,
  const bool ownHolds, const bool hasControl
) {
  all.Record(violations, holding, ownHolds, hasControl);
  if (isInScope)
    inScope.Record(violations, holding, ownHolds, hasControl);
  if (isInScope && isMatchertext)
    matchertext.Record(violations, holding, ownHolds, hasControl);
}
