//
// LanguageParser.hpp
// Author: Antoine Bastide
// Date: 29.04.2026
//

#ifndef LANGUAGE_PARSER_HPP
#define LANGUAGE_PARSER_HPP
#include <string>

#include "LanguageClassifier.hpp"

class JSON;

class LanguageParser {
  public:
    [[nodiscard]] static bool ExtractData(
      LanguageEnum language, const std::string &compilerOverride, const std::string &filePath, JSON &result
    );
    static bool ParseLanguage(const std::string &name, LanguageEnum &out);
  private:
    [[nodiscard]] static bool RunBuildCommand(
      LanguageEnum language, const std::string &compilerOverride, const std::string &filePath, std::string &out
    );
};

#endif //LANGUAGE_PARSER_HPP
