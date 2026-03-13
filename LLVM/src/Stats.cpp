//
// Stats.cpp
// Author: Antoine Bastide
// Date: 09.03.2026
//

#include "../include/Stats.hpp"

#include <iomanip>
#include <iostream>
#include <ranges>

void EmbeddedStats::DeriveStats() {
  const double n = count.load(std::memory_order_relaxed);
  const double wn = withToothpicks.load(std::memory_order_relaxed);
  const double nc = nonComplianceCount.load(std::memory_order_relaxed);
  const double wnc = withNonCompliance.load(std::memory_order_relaxed);
  const double tp = toothpicks.load(std::memory_order_relaxed);
  const double nd = nestingDepthTotal.load(std::memory_order_relaxed);
  const double vnd = validNestingDepthTotal.load(std::memory_order_relaxed);

  toothpicksAvg.store((n > 0.0 ? tp / n : 0.0), std::memory_order_relaxed);
  toothpicksAvgWith.store((wn > 0.0 ? tp / wn : 0.0), std::memory_order_relaxed);
  nonComplianceAvg.store((n > 0.0 ? nc / n : 0.0), std::memory_order_relaxed);
  complianceRate.store(100.0 * (n > 0.0 ? (n - wnc) / n : 0.0), std::memory_order_relaxed);
  nestingDepthAvg.store((n > 0.0 ? nd / n : 0.0), std::memory_order_relaxed);
  validNestingDepthAvg.store((n > 0.0 ? vnd / n : 0.0), std::memory_order_relaxed);
}

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

EmbeddedStatsSnapshot SnapshotStats(const EmbeddedStats &stats) {
  EmbeddedStatsSnapshot s{};

  #define LOAD_FIELD(name, label, ...) s.name = stats.name.load();
  EMBEDDED_STATS_FIELDS(LOAD_FIELD)
  #undef LOAD_FIELD

  return s;
}

NestedStatsSnapshot SnapshotNestedStats(const NestedStats &stats) {
  std::lock_guard lock(stats.mutex_);
  return {
    .rawLevels = stats.rawLevels_,
    .validLevels = stats.validLevels_,
  };
}

std::vector<std::tuple<std::string, double, std::string>> ToColumns(const EmbeddedStatsSnapshot &s) {
  std::vector<std::tuple<std::string, double, std::string>> cols;

  #define PUSH_FIELD(name, label, desc) cols.emplace_back(label, s.name, desc);
  EMBEDDED_STATS_FIELDS(PUSH_FIELD)
  #undef PUSH_FIELD

  return cols;
}

void PrintStatsTable(const std::vector<std::pair<std::string, EmbeddedStatsSnapshot>> &rows) {
  if (rows.empty())
    return;

  const auto firstCols = ToColumns(rows.front().second);

  std::vector<std::string> headers;
  headers.emplace_back("Metric");

  for (const auto &name: rows | std::views::keys)
    headers.push_back(name);

  std::vector<std::vector<double>> values(firstCols.size());

  for (size_t metric = 0; metric < firstCols.size(); ++metric) {
    for (const auto &snap: rows | std::views::values) {
      auto cols = ToColumns(snap);
      auto [_0, value, _1] = cols.at(metric);
      values[metric].push_back(value);
    }
  }

  // Header
  std::cout << "|";
  for (const auto &h: headers)
    std::cout << " " << h << " |";
  std::cout << '\n';

  // Separator
  std::cout << "|";
  for (size_t i = 0; i < headers.size(); ++i)
    std::cout << "---|";
  std::cout << '\n';

  // Rows
  for (size_t m = 0; m < firstCols.size(); ++m) {
    auto [name, _0, _1] = firstCols.at(m);
    std::cout << "| " << name << " |";

    for (const double c: values[m])
      std::cout << " " << c << " |";

    std::cout << '\n';
  }

  std::cout << "\n\n\n";

  std::cout << "| Statistic | Description |\n|---|---|\n";
  for (auto &[name, _, desc]: firstCols) {
    std::cout << "| " << name << " | " << desc << " |\n";
  }
}

void PrintNestedStatsTable(const std::vector<std::pair<std::string, NestedStatsSnapshot>> &rows) {
  if (rows.empty())
    return;

  size_t maxLevel = 0;
  for (const auto &[rawLevels, validLevels]: rows | std::views::values) {
    maxLevel = std::max(maxLevel, rawLevels.size());
    maxLevel = std::max(maxLevel, validLevels.size());
  }

  if (maxLevel <= 1)
    return;

  std::cout << "\n\n";
  std::cout << "| Level |";
  for (const auto &name: rows | std::views::keys)
    std::cout << " " << name << " Raw | " << name << " Valid |";
  std::cout << '\n';

  std::cout << "|---|";
  for (size_t i = 0; i < rows.size(); ++i)
    std::cout << "---|---|";
  std::cout << '\n';

  for (size_t level = 1; level < maxLevel; ++level) {
    bool hasValues = false;
    for (const auto &[rawLevels, validLevels]: rows | std::views::values) {
      const uint64_t raw = level < rawLevels.size() ? rawLevels[level] : 0;
      const uint64_t valid = level < validLevels.size() ? validLevels[level] : 0;
      if (raw != 0 || valid != 0) {
        hasValues = true;
        break;
      }
    }

    if (!hasValues)
      continue;

    std::cout << "| " << level << " |";
    for (const auto &[rawLevels, validLevels]: rows | std::views::values) {
      const uint64_t raw = level < rawLevels.size() ? rawLevels[level] : 0;
      const uint64_t valid = level < validLevels.size() ? validLevels[level] : 0;
      std::cout << " " << raw << " | " << valid << " |";
    }
    std::cout << '\n';
  }
}

std::string EscapeForLog(const std::string &s) {
  std::string out;
  out.reserve(s.size());

  for (const char c: s) {
    if (c >= 32 && c <= 126) {
      out += c;
    } else {
      char buf[5];
      std::snprintf(buf, sizeof(buf), "\\x%02x", c);
      out += buf;
    }
  }

  return out;
}

void PrintStatsMaxString(const EmbeddedStats &strings, const EmbeddedStats &docs) {
  std::cout << "String:\n"
      << " - Max Toothpicks:     \n" << EscapeForLog(strings.stringMaxToothpicks.get()) << "\n\n\n"
      << " - Max Non Compliance: \n" << EscapeForLog(strings.stringMaxNonCompliance.get()) << "\n\n\n"
      << " - Max Raw Nested:     \n" << EscapeForLog(strings.stringMaxNested.get()) << "\n\n\n"
      << " - Max Valid Nested:   \n" << EscapeForLog(strings.stringMaxValidNested.get()) << "\n\n\n"
      << "Documentation:\n"
      << " - Max Toothpicks:     \n" << EscapeForLog(docs.stringMaxToothpicks.get()) << "\n\n\n"
      << " - Max Non Compliance: \n" << EscapeForLog(docs.stringMaxNonCompliance.get()) << "\n\n\n"
      << " - Max Raw Nested:     \n" << EscapeForLog(docs.stringMaxNested.get()) << "\n\n\n"
      << " - Max Valid Nested:   \n" << EscapeForLog(docs.stringMaxValidNested.get()) << "\n\n\n";
}
