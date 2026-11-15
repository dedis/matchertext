//
// LanguageStats.hpp
// Author: Antoine Bastide
// Date: 24/03/2026
//

#ifndef LANGUAGE_STATS_HPP
#define LANGUAGE_STATS_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "LanguageClassifier.hpp"

/// Per-language entry in a snapshot (only languages with count > 0).
struct LanguageStatsEntry {
  LanguageEnum language;
  uint64_t count;
  uint64_t violations;
  uint64_t toothpicks;
  uint64_t converted; // toothpicks left after the rewrite (toothpicks for binary data)
  double percentage; // count / total * 100
};

/// Immutable snapshot of language statistics, sorted by count descending.
struct LanguageStatsSnapshot {
  uint64_t total;
  std::vector<LanguageStatsEntry> entries; // only non-zero languages, sorted by count desc
};

/// Thread-safe per-language counters for string classification results.
struct LanguageStats {
  /// Record a classified string's language, violation count, and toothpick count
  /// before and after the rewrite.
  void Record(LanguageEnum lang, uint64_t violations, uint64_t toothpicks, uint64_t converted);

  /// Produce a snapshot with percentages, sorted by count descending.
  [[nodiscard]] LanguageStatsSnapshot Snapshot() const;
  private:
    struct Entry {
      std::atomic<uint64_t> count{0};
      std::atomic<uint64_t> violations{0};
      std::atomic<uint64_t> toothpicks{0};
      std::atomic<uint64_t> converted{0};
    };

    Entry entries_[static_cast<size_t>(LanguageEnum::COUNT)]{};
};

/// Thread-safe counters of how the native raw string forms of every language hold
/// the decoded content of one host language's string literals (see
/// RawRewrite::RawFormFamilies).
struct RawFormStats {
  static constexpr size_t kMaxChecks = 64;
  static constexpr size_t kMaxForms = 64;

  /// One population of string literals.
  struct Counts {
    std::atomic<uint64_t> strings{0};
    /// Strings holding a control character other than tab, line feed, and carriage return.
    std::atomic<uint64_t> control{0};
    /// Strings that a raw form of their host language holds.
    std::atomic<uint64_t> ownForm{0};
    /// Strings that a raw form of any language holds.
    std::atomic<uint64_t> anyForm{0};
    /// Per form of RawRewrite::RawFormFamilies: strings it holds.
    std::array<std::atomic<uint64_t>, kMaxForms> forms{};
    /// Per check of RawRewrite::RawFormFamilies: strings that break it.
    std::array<std::atomic<uint64_t>, kMaxChecks> checks{};

    void Record(uint64_t violations, uint64_t holding, bool ownHolds, bool hasControl);
  };

  /// Every string literal, binary data included.
  Counts all;
  /// String literals within matchertext's scope (not binary data).
  Counts inScope;
  /// In-scope literals whose decoded content a matchertext literal holds.
  Counts matchertext;

  void Record(bool isInScope, bool isMatchertext, uint64_t violations, uint64_t holding, bool ownHolds,
              bool hasControl);
};

#endif // LANGUAGE_STATS_HPP
