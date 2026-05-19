//
// Stats.cpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#include "../include/Stats.hpp"
#include "../include/LanguageClassifier.hpp"

#include <cstdio>
#include <ostream>
#include <ranges>

namespace {
  template<typename Snapshot>
  void PrintScalarStatsTable(
    const std::vector<std::pair<std::string, Snapshot>> &rows, std::ostream &out
  ) {
    if (rows.empty())
      return;

    const auto firstCols = rows.front().second.ToColumns();

    out << "## Descriptions\n\n";
    out << "| Metric | Description |\n|---|---|\n";
    for (const auto &[name, unused, desc]: firstCols)
      out << "| " << name << " | " << desc << " |\n";

    out << "\n\n";

    std::vector<std::string> headers;
    headers.emplace_back("Metric");
    for (const auto &name: rows | std::views::keys)
      headers.push_back(name);

    std::vector<std::vector<double>> values(firstCols.size());
    for (size_t metric = 0; metric < firstCols.size(); ++metric)
      for (const auto &snap: rows | std::views::values) {
        auto cols = snap.ToColumns();
        auto [n, value, d] = cols.at(metric);
        values[metric].push_back(value);
      }

    out << "## Data\n\n";
    out << "|";
    for (const auto &h: headers)
      out << " " << h << " |";
    out << '\n';

    out << "|";
    for (size_t i = 0; i < headers.size(); ++i)
      out << "---|";
    out << '\n';

    for (size_t m = 0; m < firstCols.size(); ++m) {
      auto [name, v0, d0] = firstCols.at(m);
      out << "| " << name << " |";
      for (const double c: values[m])
        out << " " << c << " |";
      out << '\n';
    }
  }
} // namespace

void PrintStatsTable(
  const std::vector<std::pair<std::string, EmbeddedStatsSnapshot>> &rows, std::ostream &out
) {
  PrintScalarStatsTable(rows, out);
}

void PrintFileStatsTable(const FileStatsSnapshot &stats, std::ostream &out) {
  PrintScalarStatsTable(
    std::vector{std::pair<std::string, FileStatsSnapshot>{"File Stats", stats}}, out
  );
}

void PrintNestedStatsTable(
  const std::vector<std::pair<std::string, NestedStatsSnapshot>> &rows, std::ostream &out
) {
  if (rows.empty())
    return;

  size_t maxLevel = 0;
  for (const auto &[rawLevels, validLevels]: rows | std::views::values) {
    maxLevel = std::max(maxLevel, rawLevels.size());
    maxLevel = std::max(maxLevel, validLevels.size());
  }

  if (maxLevel <= 1)
    return;

  out << "| Level |";
  for (const auto &name: rows | std::views::keys)
    out << " " << name << " Raw | " << name << " Valid |";
  out << '\n';

  out << "|---|";
  for (size_t i = 0; i < rows.size(); ++i)
    out << "---|---|";
  out << '\n';

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

    out << "| " << level << " |";
    for (const auto &[rawLevels, validLevels]: rows | std::views::values) {
      const uint64_t raw = level < rawLevels.size() ? rawLevels[level] : 0;
      const uint64_t valid = level < validLevels.size() ? validLevels[level] : 0;
      out << " " << raw << " | " << valid << " |";
    }
    out << '\n';
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

void PrintLanguageStatsTable(
  const std::vector<std::pair<std::string, LanguageStatsSnapshot>> &rows, std::ostream &out
) {
  if (rows.empty())
    return;

  for (const auto &[category, snap]: rows) {
    if (snap.entries.empty())
      continue;

    out << "| " << category << " Language | Count | % | Violations | Toothpicks |\n";
    out << "|---|---|---|---|---|\n";

    for (const auto &e: snap.entries) {
      char pctBuf[16];
      std::snprintf(pctBuf, sizeof(pctBuf), "%.2f%%", e.percentage);
      out << "| " << LanguageName(e.language) << " | " << e.count << " | "
          << pctBuf << " | " << e.violations << " | " << e.toothpicks << " |\n";
    }
    out << '\n';
  }
}
