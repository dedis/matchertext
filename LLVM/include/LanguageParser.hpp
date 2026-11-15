//
// LanguageParser.hpp
// Author: Antoine Bastide
// Date: 29.04.2026
//

#ifndef LANGUAGE_PARSER_HPP
#define LANGUAGE_PARSER_HPP
#include <string>

#include "FileStats.hpp"
#include "JSON.hpp"
#include "LanguageClassifier.hpp"

class LanguageParser {
  public:
    /// Extracts string and comment samples; a file skipped without parsing
    /// returns false and reports why in `skip`.
    [[nodiscard]] static bool ExtractData(
      LanguageEnum language, const std::string &filePath, Serde::JSON &result,
      SkipReason *skip = nullptr
    );
    static bool ParseLanguage(const std::string &name, LanguageEnum &out);
};

#endif //LANGUAGE_PARSER_HPP
