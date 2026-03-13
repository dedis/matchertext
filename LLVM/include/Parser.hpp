//
// Parser.hpp
// Author: Antoine Bastide
// Date: 13/06/2025
//

#ifndef PARSER_HPP
#define PARSER_HPP

#include <string>

#include "Stats.hpp"

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

    /// All the aggregated stats relating to parsed strings
    inline static EmbeddedStats STRING_STATS{};
    inline static NestedStats STRING_NESTED_STATS{};
    /// All the aggregated stats relating to parsed docs
    inline static EmbeddedStats DOCS_STATS{};
    inline static NestedStats DOCS_NESTED_STATS{};
    /// All the aggregated stats relating to parsed docs with relaxed MatcherText
    inline static EmbeddedStats DOCS_RELAXED_STATS{};
    inline static NestedStats DOCS_RELAXED_NESTED_STATS{};
  private:
    /// Processes a string/doc and updates the given stat
    static void process(std::string &&string, EmbeddedStats &stats, NestedStats &nestedStats, bool relaxed = false);
};

#endif // PARSER_HPP
