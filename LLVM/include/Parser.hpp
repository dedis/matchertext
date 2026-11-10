//
// Parser.hpp
// Author: Antoine Bastide
// Date: 13/06/2025
//

#ifndef PARSER_HPP
#define PARSER_HPP

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
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
  RawFormStats rawForms;
  /// Example files (paths relative to the input) for the off-diagonal cells of
  /// fragments compliant x whole file compliant (strict), at most kMaxExamples each.
  static constexpr size_t kMaxExamples = 5;
  std::mutex examplesMutex;
  std::vector<std::string> fragmentsOnlyExamples;
  std::vector<std::string> wholeOnlyExamples;
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
    static bool ParseC_CPP(const std::string &path, LanguageEnum language, Serde::JSON &result);

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

    /// One distinct file content (keyed by its 64-bit FNV-1a hash): how many parsed
    /// files have it, and their string samples. Used to measure exact duplicates.
    struct FileContent {
      uint64_t files = 0;
      uint64_t strings = 0;
    };
    inline static std::unordered_map<uint64_t, FileContent> FILE_CONTENTS{};
    inline static std::mutex FILE_CONTENTS_MUTEX;

    /// What adjacent C/C++ literal segments add up to in a raw rewrite.
    struct RawLiteral {
      std::string content;  // the raw literal's content
      bool holdable = true; // every escape decodes to characters a raw literal can hold
    };

    /// Toothpicks left after rewriting a C/C++ literal body as a raw literal. If given,
    /// `literal` accumulates the segment's content and counts. `wide` is set for L, u,
    /// and U literals, whose \x and octal escapes are code units rather than bytes.
    static uint64_t CountRawStringToothpicks(std::string_view body, RawLiteral *literal = nullptr, bool wide = false);
  private:
    /// Processes a string/doc and updates the given stat
    /// @returns the number of MatcherText violations found, 0 for out-of-scope binary data
    static uint64_t process(
      std::string &&string, EmbeddedStats &stats, NestedStats &nestedStats,
      LanguageStats *langStats = nullptr, bool relaxed = false,
      std::string_view sourcePath = {}, std::string_view inputPath = {},
      EmbeddedStats *extraEmbedded = nullptr, NestedStats *extraNested = nullptr,
      LanguageStats *extraLangStats = nullptr, uint64_t convertedToothpicksHint = 0,
      bool rawHoldsHint = false, bool mHoldsHint = false,
      RawFormStats *rawForms = nullptr, uint64_t rawFormViolations = 0, uint64_t rawFormsHolding = 0,
      bool ownRawFormHint = false, bool controlHint = false
    );
};

#endif // PARSER_HPP
