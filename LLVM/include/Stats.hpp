//
// Stats.hpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#ifndef STATS_HPP
#define STATS_HPP

#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "EmbeddedStats.hpp"
#include "FileStats.hpp"
#include "LanguageStats.hpp"
#include "NestedStats.hpp"

void PrintStatsTable(const std::vector<std::pair<std::string, EmbeddedStatsSnapshot>> &rows, std::ostream &out);
void PrintFileStatsTable(const FileStatsSnapshot &stats, std::ostream &out);
void PrintNestedStatsTable(const std::vector<std::pair<std::string, NestedStatsSnapshot>> &rows, std::ostream &out);
void PrintLanguageStatsTable(const std::vector<std::pair<std::string, LanguageStatsSnapshot>> &rows, std::ostream &out);
/// Rows (language, raw form, measure) of how every language's native raw string forms hold the strings.
void PrintRawFormStatsTable(const RawFormStats &stats, std::ostream &out);

#endif // STATS_HPP
