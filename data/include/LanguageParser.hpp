//
// LanguageParser.hpp
// Author: Antoine Bastide
// Date: 29.04.2026
//

#ifndef LANGUAGE_PARSER_HPP
#define LANGUAGE_PARSER_HPP
#include <string>

#include "JSON.hpp"
#include "LanguageClassifier.hpp"

class LanguageParser {
  public:
    [[nodiscard]] static bool ExtractData(
      LanguageEnum language, const std::string &filePath, Serde::JSON &result
    );
    static bool ParseLanguage(const std::string &name, LanguageEnum &out);
};

#endif //LANGUAGE_PARSER_HPP
