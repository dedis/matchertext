//
// Parser.hpp
// Author: Antoine Bastide
// Date: 13/06/2025
//

#ifndef PARSER_HPP
#define PARSER_HPP

#include <string>

/**
 * Parser class that uses Clang's lexer to extract string literals from a C/C++ source file.
 */
class Parser final {
  public:
    /**
     * Parse a C/C++ source file and extract all string literals and comments.
     *
     * This function performs a raw lexical scan using Clang's lexer without
     * invoking the preprocessor, parser, or semantic analysis stages. Only
     * tokenization is performed. This minimizes overhead and allows fast
     * extraction of documentation text and string literals from large
     * codebases.
     *
     * Extracted elements:
     *   - All C/C++ string literal tokens:
     *       "string"
     *       L"wide"
     *       u8"utf8"
     *       u"utf16"
     *       U"utf32"
     *       R"(raw string)"
     *
     *   - Adjacent string literals are concatenated according to C/C++
     *     translation rules:
     *       "hello" "world" → "helloworld"
     *
     *   - All comment tokens:
     *       // single-line comments
     *       /\* block comments *\/
     *
     * @param path Absolute or relative path to the source file to scan.
     */
    static void ParseFile(const std::string &path);
  private:
    static void processString(std::string &&string);
};

#endif // PARSER_HPP
