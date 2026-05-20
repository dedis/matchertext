//
// LanguageParser.cpp
// Author: Antoine Bastide
// Date: 29.04.2026
//

#include <algorithm>
#include <cctype>
#include <string>

#include "../include/LanguageParser.hpp"
#include "../include/LanguageData.hpp"
#include "../include/Parser.hpp"
#include "../include/TreeSitterParser.hpp"

bool LanguageParser::ExtractData(
  const LanguageEnum language, const std::string &filePath, Serde::JSON &result
) {
  // C/C++ use the in-process clang lexer; every other supported language uses an
  // in-process tree-sitter grammar (see src/TreeSitterParser.cpp).
  if (language == LanguageEnum::C || language == LanguageEnum::CPP)
    return Parser::ParseC_CPP(filePath, result) && result.IsArray();

  if (TreeSitter::IsTreeSitterLanguage(language))
    return TreeSitter::Parse(language, filePath, result) && result.IsArray();

  return false;
}

bool LanguageParser::ParseLanguage(const std::string &name, LanguageEnum &out) {
  std::string lower(name);
  std::ranges::transform(
    lower, lower.begin(), [](const unsigned char c) {
      return std::tolower(c);
    }
  );
  out = GetLanguage(lower);
  return out != LanguageEnum::Unknown;
}
