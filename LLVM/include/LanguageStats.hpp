//
// LanguageStats.hpp
// Author: Antoine Bastide
// Date: 24/03/2026
//

#ifndef LANGUAGE_STATS_HPP
#define LANGUAGE_STATS_HPP

#include <atomic>
#include <cstdint>
#include <vector>

#include "LanguageClassifier.hpp"

/// Per-language entry in a snapshot (only languages with count > 0).
struct LanguageStatsEntry {
  Language language;
  uint64_t count;
  uint64_t violations;
  uint64_t toothpicks;
  double percentage; // count / total * 100
};

/// Immutable snapshot of language statistics, sorted by count descending.
struct LanguageStatsSnapshot {
  uint64_t total;
  std::vector<LanguageStatsEntry> entries; // only non-zero languages, sorted by count desc
};

/// Thread-safe per-language counters for string classification results.
struct LanguageStats {
  /// Record a classified string's language, violation count, and toothpick count.
  void Record(Language lang, uint64_t violations, uint64_t toothpicks);

  /// Produce a snapshot with percentages, sorted by count descending.
  [[nodiscard]] LanguageStatsSnapshot Snapshot() const;
  private:
    struct Entry {
      std::atomic<uint64_t> count{0};
      std::atomic<uint64_t> violations{0};
      std::atomic<uint64_t> toothpicks{0};
    };

    Entry entries_[static_cast<size_t>(Language::COUNT)]{};
};

#endif // LANGUAGE_STATS_HPP
