//
// Stats.hpp
// Author: Antoine Bastide
// Date: 18.03.2026
//

#ifndef STATS_HPP
#define STATS_HPP

#include <string>
#include <utility>
#include <vector>

#include "EmbeddedStats.hpp"
#include "FileStats.hpp"
#include "LanguageStats.hpp"
#include "NestedStats.hpp"

void PrintStatsTable(const std::vector<std::pair<std::string, EmbeddedStatsSnapshot>> &rows);
void PrintFileStatsTable(const FileStatsSnapshot &stats);
void PrintNestedStatsTable(const std::vector<std::pair<std::string, NestedStatsSnapshot>> &rows);
void PrintStatsMaxString(const EmbeddedStats &strings, const EmbeddedStats &docs);
void PrintLanguageStatsTable(const std::vector<std::pair<std::string, LanguageStatsSnapshot>> &rows);

#endif // STATS_HPP
