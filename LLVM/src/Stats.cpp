//
// Stats.cpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#include "../include/Stats.hpp"
#include "../include/LanguageClassifier.hpp"

#include <cstdio>
#include <iostream>
#include <ranges>

namespace {
  template<typename Snapshot>
  void PrintScalarStatsTable(const std::vector<std::pair<std::string, Snapshot>> &rows) {
    if (rows.empty())
      return;

    const auto firstCols = rows.front().second.ToColumns();

    std::vector<std::string> headers;
    headers.emplace_back("Metric");

    for (const auto &name: rows | std::views::keys)
      headers.push_back(name);

    std::vector<std::vector<double>> values(firstCols.size());

    for (size_t metric = 0; metric < firstCols.size(); ++metric) {
      for (const auto &snap: rows | std::views::values) {
        auto cols = snap.ToColumns();
        auto [_0, value, _1] = cols.at(metric);
        values[metric].push_back(value);
      }
    }

    std::cout << "|";
    for (const auto &h: headers)
      std::cout << " " << h << " |";
    std::cout << '\n';

    std::cout << "|";
    for (size_t i = 0; i < headers.size(); ++i)
      std::cout << "---|";
    std::cout << '\n';

    for (size_t m = 0; m < firstCols.size(); ++m) {
      auto [name, _0, _1] = firstCols.at(m);
      std::cout << "| " << name << " |";

      for (const double c: values[m])
        std::cout << " " << c << " |";

      std::cout << '\n';
    }

    std::cout << "\n\n\n";

    std::cout << "| Statistic | Description |\n|---|---|\n";
    for (auto &[name, _, desc]: firstCols)
      std::cout << "| " << name << " | " << desc << " |\n";
  }
} // namespace

void PrintStatsTable(const std::vector<std::pair<std::string, EmbeddedStatsSnapshot>> &rows) {
  PrintScalarStatsTable(rows);
}

void PrintFileStatsTable(const FileStatsSnapshot &stats) {
  PrintScalarStatsTable(std::vector{std::pair<std::string, FileStatsSnapshot>{"File Stats", stats}});
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

void PrintLanguageStatsTable(const std::vector<std::pair<std::string, LanguageStatsSnapshot>> &rows) {
  if (rows.empty())
    return;

  for (const auto &[category, snap]: rows) {
    if (snap.entries.empty())
      continue;

    std::cout << "\n\n";
    std::cout << "| " << category << " Language | Count | % | Violations | Toothpicks |\n";
    std::cout << "|---|---|---|---|---|\n";

    for (const auto &e: snap.entries) {
      char pctBuf[16];
      std::snprintf(pctBuf, sizeof(pctBuf), "%.2f%%", e.percentage);
      std::cout << "| " << LanguageName(e.language) << " | " << e.count << " | "
          << pctBuf << " | " << e.violations << " | " << e.toothpicks << " |\n";
    }
  }
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
