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
  const double nc = nonComplianceCount.load(std::memory_order_relaxed);
  const double wnc = withNonCompliance.load(std::memory_order_relaxed);
  const double tp = toothpicks.load(std::memory_order_relaxed);
  const double nd = nestingDepthTotal.load(std::memory_order_relaxed);

  toothpicksAvg.store(n > 0.0 ? tp / n : 0.0, std::memory_order_relaxed);
  nonComplianceAvg.store(n > 0.0 ? nc / n : 0.0, std::memory_order_relaxed);
  complianceRate.store(n > 0.0 ? (n - wnc) / n : 0.0, std::memory_order_relaxed);
  nestingDepthAvg.store(n > 0.0 ? nd / n : 0.0, std::memory_order_relaxed);
}

EmbeddedStatsSnapshot SnapshotStats(const EmbeddedStats &stats) {
  EmbeddedStatsSnapshot s{};

  #define LOAD_FIELD(name, label) s.name = stats.name.load();
  EMBEDDED_STATS_FIELDS(LOAD_FIELD)
  #undef LOAD_FIELD

  return s;
}

std::vector<std::pair<std::string, double>> ToColumns(const EmbeddedStatsSnapshot &s) {
  std::vector<std::pair<std::string, double>> cols;

  #define PUSH_FIELD(name, label) cols.emplace_back(label, s.name);
  EMBEDDED_STATS_FIELDS(PUSH_FIELD)
  #undef PUSH_FIELD

  return cols;
}

void PrintStatsTable(
  const std::vector<std::pair<std::string, EmbeddedStatsSnapshot>> &rows
) {
  if (rows.empty())
    return;

  const auto firstCols = ToColumns(rows.front().second);

  std::vector<std::string> headers;
  headers.emplace_back("Metric");

  for (const auto &name: rows | std::views::keys)
    headers.push_back(name);

  std::vector<size_t> widths(headers.size());
  for (size_t i = 0; i < headers.size(); ++i)
    widths[i] = headers[i].size();

  std::vector<std::vector<double>> values(firstCols.size());

  for (size_t metric = 0; metric < firstCols.size(); ++metric) {
    for (const auto &snap: rows | std::views::values) {
      auto cols = ToColumns(snap);
      values[metric].push_back(cols[metric].second);
    }
  }

  for (size_t m = 0; m < firstCols.size(); ++m) {
    widths[0] = std::max(widths[0], firstCols[m].first.size());

    for (size_t c = 0; c < values[m].size(); ++c)
      widths[c + 1] = std::max(
        widths[c + 1],
        std::to_string(values[m][c]).size()
      );
  }

  std::cout << std::left << std::setw(static_cast<int>(widths[0]) + 2) << headers[0];

  for (size_t i = 1; i < headers.size(); ++i)
    std::cout << std::right << std::setw(static_cast<int>(widths[i]) + 2) << headers[i];

  std::cout << '\n';

  size_t totalWidth = 0;
  for (const auto w: widths)
    totalWidth += w + 2;

  std::cout << std::string(totalWidth, '-') << '\n';

  for (size_t m = 0; m < firstCols.size(); ++m) {
    std::cout << std::left << std::setw(static_cast<int>(widths[0]) + 2)
        << firstCols[m].first;

    for (size_t c = 0; c < values[m].size(); ++c)
      std::cout << std::right << std::setw(static_cast<int>(widths[c + 1]) + 2)
          << values[m][c];

    std::cout << '\n';
  }
}
