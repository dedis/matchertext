//
// Parser.hpp
// Author: Antoine Bastide
// Date: 13/06/2025
//

#ifndef PARSER_HPP
#define PARSER_HPP

#include <string>
#include <string_view>
#include <vector>

#include "FileStats.hpp"
#include "JSON.hpp"
#include "LanguageStats.hpp"
#include "Stats.hpp"

/// Owned stats for a single source language (used for per-language output).
struct PerLanguageStats {
  EmbeddedStats stringStats;
  NestedStats stringNestedStats;
  EmbeddedStats docsStats;
  NestedStats docsNestedStats;
  EmbeddedStats docsRelaxedStats;
  NestedStats docsRelaxedNestedStats;
  FileStats fileStats;
  LanguageStats langStats;
};

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
     * @param result
     */
    static bool ParseC_CPP(const std::string &path, Serde::JSON &result);

    static void GatherStatistics(
      Serde::JSON &&json, const std::string &path, std::string_view inputPath = {}, PerLanguageStats *perLang = nullptr
    );

    /// Parse a C/C++ file and gather statistics in one step.
    static void ParseFile(const std::string &filePath, const std::string &inputPath);

    /// Enable per-language debug sampling; samples are written under outputDir/languages/.
    static void ConfigureDebugLanguages(
      const std::vector<std::string> &inputPaths, const std::string &outputDir
    );

    static void FlushDebugLanguageLogs();

    /// All the aggregated stats relating to parsed strings
    inline static EmbeddedStats STRING_STATS{};
    inline static NestedStats STRING_NESTED_STATS{};
    /// All the aggregated stats relating to parsed docs
    inline static EmbeddedStats DOCS_STATS{};
    inline static NestedStats DOCS_NESTED_STATS{};
    /// All the aggregated stats relating to parsed docs with relaxed MatcherText
    inline static EmbeddedStats DOCS_RELAXED_STATS{};
    inline static NestedStats DOCS_RELAXED_NESTED_STATS{};
    /// All the aggregated file level statistics
    inline static FileStats FILE_STATS{};
    /// Per-language classification statistics for strings
    inline static LanguageStats STRING_LANG_STATS{};
  private:
    /// Processes a string/doc and updates the given stat
    /// @returns the number of MatcherText violations found
    static uint64_t process(
      std::string &&string, EmbeddedStats &stats, NestedStats &nestedStats,
      LanguageStats *langStats = nullptr, bool relaxed = false,
      std::string_view sourcePath = {}, std::string_view inputPath = {},
      EmbeddedStats *extraEmbedded = nullptr, NestedStats *extraNested = nullptr,
      LanguageStats *extraLangStats = nullptr
    );
};

#endif // PARSER_HPP
