//
// NestedStats.hpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#ifndef NESTED_STATS_HPP
#define NESTED_STATS_HPP

#include <cstdint>
#include <mutex>
#include <vector>

struct NestedStatsSnapshot {
  std::vector<uint64_t> rawLevels;
  std::vector<uint64_t> validLevels;
};

struct NestedStats {
  void Record(uint64_t rawDepth, uint64_t validDepth);
  NestedStatsSnapshot Snapshot() const;
  private:
    mutable std::mutex mutex_;
    std::vector<uint64_t> rawLevels_;
    std::vector<uint64_t> validLevels_;

    static void RecordLevel(std::vector<uint64_t> &levels, uint64_t depth);
};

#endif // NESTED_STATS_HPP
