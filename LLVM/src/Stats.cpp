//
// Stats.cpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#include "../include/Stats.hpp"
#include "../include/LanguageClassifier.hpp"
#include "../include/RawRewrite.hpp"

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

    out << "| " << category << " Language | Count | % | Violations | Toothpicks | Converted Toothpicks | Reduction |\n";
    out << "|---|---|---|---|---|---|---|\n";

    for (const auto &e: snap.entries) {
      char pctBuf[16];
      std::snprintf(pctBuf, sizeof(pctBuf), "%.2f%%", e.percentage);
      /// Binary data is out of matchertext's scope, so it has no reduction.
      char reductionBuf[16] = "-";
      if (InMatchertextScope(e.language) && e.toothpicks > 0)
        std::snprintf(reductionBuf, sizeof(reductionBuf), "%.2f%%",
                      100.0 * static_cast<double>(e.toothpicks - e.converted) / static_cast<double>(e.toothpicks));
      out << "| " << LanguageName(e.language) << " | " << e.count << " | "
          << pctBuf << " | " << e.violations << " | " << e.toothpicks << " | " << e.converted << " | "
          << reductionBuf << " |\n";
    }
    out << '\n';
  }
}

namespace {
  /// Markdown table cell text: escapes the characters Markdown would interpret.
  std::string MarkdownCell(const std::string_view text) {
    std::string out;
    for (const char c: text) {
      if (c == '\\' || c == '`' || c == '|' || c == '*' || c == '_')
        out += '\\';
      out += c;
    }
    return out;
  }
}

void PrintRawFormStatsTable(const RawFormStats &stats, std::ostream &out) {
  const auto row = [&](const std::string_view language, const std::string_view form,
                       const std::string_view measure, const auto field) {
    out << "| " << MarkdownCell(language) << " | " << MarkdownCell(form) << " | " << MarkdownCell(measure) << " | "
        << field(stats.all) << " | " << field(stats.inScope) << " | " << field(stats.matchertext) << " |\n";
  };
  const auto load = [](const std::atomic<uint64_t> &n) { return n.load(std::memory_order_relaxed); };

  out << "| Language | Raw Form | Measure | All Strings | In-Scope Strings | Matchertext Strings |\n";
  out << "|---|---|---|---|---|---|\n";
  row("-", "-", "String literals", [&](const RawFormStats::Counts &c) { return load(c.strings); });
  row("-", "-", "Contain control characters", [&](const RawFormStats::Counts &c) { return load(c.control); });
  row("Own language", "Any form", "Holds", [&](const RawFormStats::Counts &c) { return load(c.ownForm); });
  row("Any language", "Any form", "Holds", [&](const RawFormStats::Counts &c) { return load(c.anyForm); });

  size_t check = 0, form = 0;
  for (const auto &family: RawRewrite::RawFormFamilies())
    for (size_t i = 0; i < family.checks.size() && form < RawFormStats::kMaxForms; ++form) {
      const std::string_view name = family.checks[i].form;
      row(family.name, name, "Holds", [&](const RawFormStats::Counts &c) { return load(c.forms[form]); });
      for (; i < family.checks.size() && family.checks[i].form == name; ++i, ++check)
        if (check < RawFormStats::kMaxChecks)
          row(family.name, name, family.checks[i].constraint,
              [&](const RawFormStats::Counts &c) { return load(c.checks[check]); });
    }
  out << '\n';
}
