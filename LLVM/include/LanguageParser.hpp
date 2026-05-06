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
      Language language, const std::string &compilerOverride, const std::string &filePath, Serde::JSON &result
    );
    static bool ParseLanguage(const std::string &name, Language &out);
  private:
    [[nodiscard]] static bool RunBuildCommand(
      Language language, const std::string &compilerOverride, const std::string &filePath, std::string &out
    );
};

#endif //LANGUAGE_PARSER_HPP
