//
// NestedStats.cpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#include "../include/NestedStats.hpp"

#include <mutex>

void NestedStats::RecordLevel(std::vector<uint64_t> &levels, const uint64_t depth) {
  if (depth == 0)
    return;

  if (levels.size() <= depth)
    levels.resize(depth + 1, 0);

  ++levels[depth];
}

void NestedStats::Record(const uint64_t rawDepth, const uint64_t validDepth) {
  std::lock_guard lock(mutex_); // Auto unlocked when out of scope with RAII
  RecordLevel(rawLevels_, rawDepth);
  RecordLevel(validLevels_, validDepth);
}

NestedStatsSnapshot NestedStats::Snapshot() const {
  std::lock_guard lock(mutex_);
  return {
    .rawLevels = rawLevels_,
    .validLevels = validLevels_,
  };
}
