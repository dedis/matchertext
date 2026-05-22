//
// Parser.cpp
// Author: Antoine Bastide
// Date: 13/06/2025
//

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <system_error>

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>

#include "../include/Parser.hpp"
#include "../include/LanguageClassifier.hpp"
#include "../include/MatcherText.hpp"

namespace Serde {
  class JSON;
}

namespace fs = std::filesystem;

namespace {
  constexpr size_t kMaxDebugSamplesPerLanguage = 1000;
  constexpr size_t kLanguageCount = static_cast<size_t>(LanguageEnum::COUNT);

  struct DebugLanguageSample {
    std::string sourcePath;
    float confidence = 0.0f;
    uint64_t violations = 0;
    uint64_t toothpicks = 0;
    std::string text;
  };

  struct DebugLanguageBucket {
    std::mutex mutex;
    uint64_t seen = 0;
    std::vector<DebugLanguageSample> samples;
    std::mt19937_64 rng{0};
  };

  void EnsureDirectoryPrepared(const fs::path &directory) {
    std::error_code ec;
    if (ec)
      throw std::runtime_error(
        "Failed to clear debug language directory '" + directory.string() + "': " + ec.message()
      );

    fs::create_directories(directory, ec);
    if (ec)
      throw std::runtime_error(
        "Failed to create debug language directory '" +
        directory.string() + "': " + ec.message()
      );
  }

  uint64_t SeedDebugBucket(const std::string_view, const LanguageEnum language) {
    uint64_t seed = 14695981039346656037ull;
    seed ^= static_cast<uint64_t>(language) + 0x9e3779b97f4a7c15ull;
    return seed;
  }

  std::string EscapeDebugString(const std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (const unsigned char c: text) {
      switch (c) {
        case '\\':
          escaped += "\\\\";
          break;
        case '\n':
          escaped += "\\n";
          break;
        case '\r':
          escaped += "\\r";
          break;
        case '\t':
          escaped += "\\t";
          break;
        default:
          if (c >= 32 && c <= 126) {
            escaped.push_back(static_cast<char>(c));
          } else {
            char buf[5];
            std::snprintf(buf, sizeof(buf), "\\x%02x", c);
            escaped += buf;
          }
          break;
      }
    }

    return escaped;
  }

  class DebugLanguageLogger {
    public:
      void Configure(const std::vector<std::string> &inputPaths, const std::string &outputDir) {
        std::lock_guard lock(mutex_);
        enabled_ = true;
        root_ = fs::path(outputDir) / "languages";
        EnsureDirectoryPrepared(root_);
        for (size_t idx = 0; idx < kLanguageCount; idx++)
          buckets_[idx].rng.seed(
            SeedDebugBucket("", static_cast<LanguageEnum>(idx))
          );
      }

      void Record(
        const std::string_view inputPath, const LanguageEnum language,
        const std::string_view sourcePath, const std::string_view text,
        const float confidence, const uint64_t violations,
        const uint64_t toothpicks
      ) { {
          std::lock_guard lock(mutex_);
          if (!enabled_)
            return;
        }

        auto &bucket = buckets_[static_cast<size_t>(language)];
        std::lock_guard bucketLock(bucket.mutex);

        const DebugLanguageSample sample{
          std::string(sourcePath), confidence, violations, toothpicks,
          EscapeDebugString(text),
        };

        bucket.seen++;
        if (bucket.samples.size() < kMaxDebugSamplesPerLanguage) {
          bucket.samples.push_back(sample);
          return;
        }

        std::uniform_int_distribution<uint64_t> dist(0, bucket.seen - 1);
        if (const uint64_t index = dist(bucket.rng); index < bucket.samples.size())
          bucket.samples[static_cast<size_t>(index)] = sample;
      }

      void Flush() {
        std::lock_guard lock(mutex_);
        if (!enabled_)
          return;

        // Clear stale .txt files from a previous run.
        std::error_code ec;
        for (const auto &entry: fs::directory_iterator(root_, ec)) {
          if (ec)
            break;
          if (!entry.is_regular_file(ec) || ec)
            continue;
          if (entry.path().extension() != ".txt")
            continue;
          fs::remove(entry.path(), ec);
          if (ec)
            throw std::runtime_error(
              "Failed to clear stale debug language log '" +
              entry.path().string() + "': " + ec.message()
            );
        }

        for (size_t idx = 0; idx < kLanguageCount; idx++) {
          auto &bucket = buckets_[idx];
          std::lock_guard bucketLock(bucket.mutex);
          if (bucket.seen == 0 || bucket.samples.empty())
            continue;

          const fs::path outPath =
              root_ / (std::string(LanguageName(static_cast<LanguageEnum>(idx))) + ".txt");
          std::ofstream out(outPath, std::ios::trunc);
          if (!out)
            throw std::runtime_error(
              "Failed to open debug language log '" + outPath.string() + "' for writing."
            );

          out << "# Language: " << LanguageName(static_cast<LanguageEnum>(idx)) << '\n'
              << "# TotalSeen: " << bucket.seen << '\n'
              << "# SampleCount: " << bucket.samples.size() << "\n\n";

          for (size_t i = 0; i < bucket.samples.size(); i++) {
            const auto &sample = bucket.samples[i];
            out << "=== Sample " << (i + 1) << " ===\n"
                << "# Source: " << sample.sourcePath << '\n'
                << "# Confidence: " << std::fixed << std::setprecision(3)
                << sample.confidence << '\n'
                << "# Violations: " << sample.violations << '\n'
                << "# Toothpicks: " << sample.toothpicks << '\n'
                << sample.text << "\n\n";
          }
        }
      }
    private:
      bool enabled_ = false;
      fs::path root_;
      std::mutex mutex_;
      std::array<DebugLanguageBucket, kLanguageCount> buckets_{};
  };

  DebugLanguageLogger &GetDebugLanguageLogger() {
    static DebugLanguageLogger logger;
    return logger;
  }
} // namespace

void AtomicAdd(std::atomic<double> &dst, const double delta) {
  double cur = dst.load(std::memory_order_relaxed);
  while (!dst.compare_exchange_weak(cur, cur + delta, std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

bool AtomicMax(std::atomic<double> &dst, const double value) {
  double cur = dst.load(std::memory_order_relaxed);
  while (value > cur) {
    if (dst.compare_exchange_weak(cur, value, std::memory_order_relaxed, std::memory_order_relaxed))
      return true;
  }

  return false;
}

static bool IsStringToken(const clang::Token &tok) {
  return tok.is(clang::tok::string_literal) ||
         tok.is(clang::tok::wide_string_literal) ||
         tok.is(clang::tok::utf8_string_literal) ||
         tok.is(clang::tok::utf16_string_literal) ||
         tok.is(clang::tok::utf32_string_literal);
}

// Returns the source-form body of one string token.
// Normal strings keep escapes as written.
// Raw strings return verbatim raw body.
// `isRaw` is set to true iff the token is a raw string literal (R"delim(...)delim");
// this lets callers count toothpicks per-segment without re-decoding raw bodies.
static std::string ExtractLiteralBody(std::string_view spelling, bool &isRaw) {
  isRaw = false;
  const size_t quote = spelling.find('"');
  if (quote == std::string_view::npos)
    return {};

  if (const bool raw = quote > 0 && spelling[quote - 1] == 'R'; !raw) {
    const size_t end = spelling.rfind('"');
    if (end == std::string_view::npos || end <= quote)
      return {};
    return std::string(spelling.substr(quote + 1, end - quote - 1));
  }

  isRaw = true;
  // Raw form: prefix R"delim(body)delim"
  const size_t open = spelling.find('(', quote);
  if (open == std::string_view::npos)
    return {};

  const std::string delim(spelling.substr(quote + 1, open - quote - 1));
  const std::string suffix = ")" + delim + "\"";
  const size_t close = spelling.rfind(suffix);
  if (close == std::string_view::npos || close <= open)
    return {};

  return std::string(spelling.substr(open + 1, close - open - 1));
}

// Counts the backslash bytes that would remain if a C/C++ string literal body were
// rewritten verbatim inside a raw string literal R"(...)" — i.e. the number of '\'
// characters in the *decoded* content. Escape sequences are decoded semantically:
// only sequences that actually produce a 0x5C code unit are counted (\\, octal \ooo,
// hex \xH..., the \uXXXX / \UXXXXXXXX universal-character-name forms, and the C++23
// delimited forms \x{...} / \o{...} / \u{...} / \N{REVERSE SOLIDUS}). `body` is
// expected to already have line splices removed
// (clang::Lexer::getSpelling does this); bodies taken from raw literals — or from
// normal+raw adjacent-literal concatenations — are decoded as if normal and may be
// over-collapsed, which is an accepted approximation here.
uint64_t Parser::CountRawStringToothpicks(std::string_view body) {
  const auto hexDigit = [](const unsigned char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  const auto octDigit = [](const unsigned char c) -> int {
    return c >= '0' && c <= '7' ? c - '0' : -1;
  };

  const size_t n = body.size();

  // Reads exactly `digits` hex digits starting at body[i]; advances i past them on success.
  const auto parseFixedHex = [&](size_t &i, const int digits, uint32_t &value) -> bool {
    value = 0;
    for (int k = 0; k < digits; ++k) {
      if (i >= n)
        return false;
      const int d = hexDigit(static_cast<unsigned char>(body[i]));
      if (d < 0)
        return false;
      value = value * 16u + static_cast<uint32_t>(d);
      ++i;
    }
    return true;
  };

  // Reads a "{ digits }" run in the given base starting at body[i]; advances i past the
  // closing brace on success. Requires at least one digit.
  const auto parseBracedNumber = [&](size_t &i, const int base, uint32_t &value) -> bool {
    if (i >= n || body[i] != '{')
      return false;
    ++i;

    value = 0;
    bool any = false;
    while (i < n && body[i] != '}') {
      const int d = base == 16
                      ? hexDigit(static_cast<unsigned char>(body[i]))
                      : octDigit(static_cast<unsigned char>(body[i]));
      if (d < 0)
        return false;
      value = value * static_cast<uint32_t>(base) + static_cast<uint32_t>(d);
      any = true;
      ++i;
    }
    if (i >= n || body[i] != '}')
      return false;
    ++i;
    return any;
  };

  uint64_t count = 0;
  for (size_t i = 0; i < n;) {
    if (body[i] != '\\') {
      ++i;
      continue;
    }

    // Start of an escape sequence; the leading backslash itself does not survive.
    if (++i >= n)
      break; // dangling backslash — not valid source, ignore.

    const auto e = static_cast<unsigned char>(body[i]);

    if (e == '\\') { // \\ -> one literal backslash survives
      ++count;
      ++i;
      continue;
    }

    if (e == 'x') { // \xH... or (C++23) \x{ H... }
      ++i;
      uint32_t value = 0;
      if (i < n && body[i] == '{') {
        if (parseBracedNumber(i, 16, value) && value == 0x5Cu)
          ++count;
        continue;
      }
      bool any = false;
      while (i < n) {
        const int d = hexDigit(static_cast<unsigned char>(body[i]));
        if (d < 0)
          break;
        value = value * 16u + static_cast<uint32_t>(d);
        any = true;
        ++i;
      }
      if (any && value == 0x5Cu)
        ++count;
      continue;
    }

    if (e == 'o') { // (C++23) \o{ O... }
      ++i;
      uint32_t value = 0;
      if (i < n && body[i] == '{' && parseBracedNumber(i, 8, value) && value == 0x5Cu)
        ++count;
      continue;
    }

    if (e == 'u' || e == 'U') { // \uHHHH, \UHHHHHHHH, or (C++23) \u{ H... }
      ++i;
      uint32_t value = 0;
      if (i < n && body[i] == '{') {
        if (parseBracedNumber(i, 16, value) && value == 0x5Cu)
          ++count;
        continue;
      }
      if (parseFixedHex(i, e == 'u' ? 4 : 8, value) && value == 0x5Cu)
        ++count;
      continue;
    }

    if (e == 'N') { // (C++23) named universal character escape \N{ NAME }
      ++i;
      constexpr std::string_view reverseSolidus = "{REVERSE SOLIDUS}";
      if (body.substr(i, reverseSolidus.size()) == reverseSolidus) {
        ++count;
        i += reverseSolidus.size();
      }
      continue;
    }

    if (e >= '0' && e <= '7') { // \ooo octal escape, up to 3 digits
      uint32_t value = 0;
      for (int d = 0; d < 3 && i < n; ++d) {
        const int od = octDigit(static_cast<unsigned char>(body[i]));
        if (od < 0)
          break;
        value = value * 8u + static_cast<uint32_t>(od);
        ++i;
      }
      if (value == 0x5Cu)
        ++count;
      continue;
    }

    // Simple escapes (\n \t \r \v \f \a \b \" \' \? ...): none decodes to a backslash.
    ++i;
  }
  return count;
}

void Parser::ParseFile(const std::string &filePath, const std::string &inputPath) {
  if (Serde::JSON result; ParseC_CPP(filePath, result) && result.IsArray())
    GatherStatistics(std::move(result), filePath, inputPath);
}

void Parser::ConfigureDebugLanguages(
  const std::vector<std::string> &inputPaths, const std::string &outputDir
) {
  GetDebugLanguageLogger().Configure(inputPaths, outputDir);
}

void Parser::FlushDebugLanguageLogs() {
  GetDebugLanguageLogger().Flush();
}

bool Parser::ParseC_CPP(const std::string &path, Serde::JSON &result) {
  /// Read-only after construction — safe to share across threads.
  static clang::DiagnosticOptions diagOpts;
  static auto diagIDs = llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>();
  static clang::IgnoringDiagConsumer diagConsumer;
  static clang::LangOptions langOpts = [] {
    clang::LangOptions opts;
    opts.CPlusPlus = true;
    opts.CPlusPlus20 = true;
    return opts;
  }();
  static clang::FileSystemOptions fsOpts;

  /// Mutable — must be thread-local to avoid data races under OpenMP.
  thread_local clang::DiagnosticsEngine diags(diagIDs, diagOpts, &diagConsumer, false);
  thread_local clang::FileManager fileMgr(fsOpts);

  /// Source manager bound to this file.
  clang::SourceManager srcMgr(diags, fileMgr);

  /// Load file contents into a memory buffer.
  auto buffer = fileMgr.getBufferForFile(path);
  if (!buffer) {
    std::cerr << "Failed to open file: " << path << std::endl;
    return false;
  }

  const llvm::MemoryBuffer *memBuf = buffer->get();

  /// Register the file inside the source manager.
  clang::FileID fileID = srcMgr.createFileID(std::move(*buffer));
  srcMgr.setMainFileID(fileID);

  /// Construct a raw lexer over the memory buffer.
  const char *bufStart = memBuf->getBufferStart();
  const char *bufEnd = memBuf->getBufferEnd();
  clang::Lexer lexer(srcMgr.getLocForStartOfFile(fileID), langOpts, bufStart, bufStart, bufEnd);
  lexer.SetCommentRetentionState(true);

  if (!result.IsArray())
    result = Serde::JSON::Array();

  /// Tokenize the file until EOF.
  clang::Token tok{};
  while (true) {
    lexer.LexFromRawLexer(tok);
    if (tok.is(clang::tok::eof))
      break;

    /// Handle string literal tokens.
    if (IsStringToken(tok)) {
      std::string value;
      uint64_t convertedToothpicks = 0;
      clang::Token current = tok;

      /// Concatenate adjacent string literals ("a" "b").
      do {
        const std::string spelling = clang::Lexer::getSpelling(current, srcMgr, langOpts);
        bool isRaw = false;
        std::string body = ExtractLiteralBody(spelling, isRaw);

        /// Toothpicks that survive rewriting this segment as a raw string literal,
        /// counted per-segment so adjacent normal+raw literals are each handled in
        /// their own mode. A raw literal is already raw, so its backslashes are
        /// literal and kept verbatim; a normal literal's escapes decode down to the
        /// backslashes their content actually holds.
        convertedToothpicks += isRaw
          ? static_cast<uint64_t>(std::count(body.begin(), body.end(), '\\'))
          : CountRawStringToothpicks(body);

        value += body;
        lexer.LexFromRawLexer(current);
      } while (IsStringToken(current));

      tok = current;
      result.PushBack(Serde::JSON::Object({
        {"kind", Serde::JSON("string")},
        {"value", Serde::JSON(value)},
        {"convertedToothpicks", Serde::JSON(static_cast<double>(convertedToothpicks))}
      }));
      continue;
    }

    /// Capture comment tokens.
    if (tok.is(clang::tok::comment)) {
      std::string comment = clang::Lexer::getSpelling(tok, srcMgr, langOpts);
      result.PushBack(Serde::JSON::Object({{"kind", Serde::JSON("comment")}, {"value", Serde::JSON(comment)}}));
    }
  }

  return true;
}

void Parser::GatherStatistics(
  Serde::JSON &&json, const std::string &path, const std::string_view inputPath, PerLanguageStats *perLang
) {
  uint64_t fileViolations = 0;
  uint64_t fileViolationsRelaxed = 0;

  for (auto e: json.GetArray()) {
    std::string value = e["value"].GetString();
    if (e["kind"].GetString() == "string") {
      const uint64_t convertedToothpicks = e.Contains("convertedToothpicks")
        ? static_cast<uint64_t>(e["convertedToothpicks"].GetNumber())
        : 0;
      const uint64_t violations = process(
        std::move(value), STRING_STATS, STRING_NESTED_STATS, &STRING_LANG_STATS, false, path,
        inputPath.empty() ? std::string_view(path) : inputPath,
        perLang ? &perLang->stringStats : nullptr,
        perLang ? &perLang->stringNestedStats : nullptr,
        perLang ? &perLang->langStats : nullptr,
        convertedToothpicks
      );
      fileViolations += violations;
      fileViolationsRelaxed += violations;
    } else {
      fileViolations += process(
        std::string(value), DOCS_STATS, DOCS_NESTED_STATS, nullptr, false, {}, {},
        perLang ? &perLang->docsStats : nullptr,
        perLang ? &perLang->docsNestedStats : nullptr
      );
      fileViolationsRelaxed += process(
        std::move(value), DOCS_RELAXED_STATS, DOCS_RELAXED_NESTED_STATS, nullptr, true, {}, {},
        perLang ? &perLang->docsRelaxedStats : nullptr,
        perLang ? &perLang->docsRelaxedNestedStats : nullptr
      );
    }
  }

  auto updateFileStats = [](FileStats &fs, const uint64_t fv, const uint64_t fvr) {
    AtomicAdd(fs.count, 1.0);
    if (fv > 0)
      AtomicAdd(fs.withViolation, 1.0);
    AtomicAdd(fs.violationCount, static_cast<double>(fv));
    AtomicMax(fs.violationMax, static_cast<double>(fv));
    if (fvr > 0)
      AtomicAdd(fs.withViolationRelaxed, 1.0);
    AtomicAdd(fs.violationCountRelaxed, static_cast<double>(fvr));
    AtomicMax(fs.violationMaxRelaxed, static_cast<double>(fvr));
  };

  updateFileStats(FILE_STATS, fileViolations, fileViolationsRelaxed);
  if (perLang)
    updateFileStats(perLang->fileStats, fileViolations, fileViolationsRelaxed);
}

uint64_t Parser::process(
  std::string &&string, EmbeddedStats &stats, NestedStats &nestedStats,
  LanguageStats *langStats, const bool relaxed,
  const std::string_view sourcePath, const std::string_view inputPath,
  EmbeddedStats *extraEmbedded, NestedStats *extraNested, LanguageStats *extraLangStats,
  const uint64_t convertedToothpicksHint
) {
  uint64_t toothpicks = 0;
  for (const unsigned char c: string) {
    if (c == '\\')
      ++toothpicks;
  }

  const auto [unmatched, maxDepth, maxValidDepth, rawChars] = AnalyzeMatcherText(string, relaxed);

  /// Toothpick count after rewriting this sample under the matchertext rule: a
  /// matchertext-compliant string literal is replaced by an equivalent C++ raw
  /// string literal (langStats is non-null only for string literals); everything
  /// else (non-compliant strings, comments) is counted unchanged. The converted
  /// count for compliant literals is computed per-segment at extraction time
  /// (convertedToothpicksHint) so raw and adjacent normal+raw literals are each
  /// counted in their own mode rather than re-decoding the concatenated body.
  const bool compliantStringLiteral = langStats != nullptr && unmatched == 0;
  const uint64_t convertedToothpicks =
      compliantStringLiteral ? convertedToothpicksHint : toothpicks;

  if (langStats != nullptr) {
    /// Classify the embedded language and record per-language stats only for strings.
    if (const auto [lang, confidence] = ClassifyString(string); lang != LanguageEnum::Unknown) {
      langStats->Record(lang, unmatched, toothpicks);
      if (extraLangStats)
        extraLangStats->Record(lang, unmatched, toothpicks);
      GetDebugLanguageLogger().Record(
        inputPath, lang, sourcePath, string,
        confidence, unmatched, toothpicks
      );
    }
  }

  nestedStats.Record(maxDepth, maxValidDepth);
  if (extraNested)
    extraNested->Record(maxDepth, maxValidDepth);

  auto applyEmbedded = [&](EmbeddedStats &s) {
    AtomicAdd(s.count, 1.0);
    AtomicAdd(s.rawChars, static_cast<double>(rawChars));

    if (toothpicks > 0)
      AtomicAdd(s.withToothpicks, 1.0);
    AtomicAdd(s.toothpicks, static_cast<double>(toothpicks));
    if (AtomicMax(s.toothpicksMax, static_cast<double>(toothpicks)))
      s.stringMaxToothpicks.set(string);

    if (convertedToothpicks > 0)
      AtomicAdd(s.withToothpicksConverted, 1.0);
    AtomicAdd(s.toothpicksConverted, static_cast<double>(convertedToothpicks));
    AtomicMax(s.toothpicksConvertedMax, static_cast<double>(convertedToothpicks));

    /// Per-sample reduction (%), accumulated so DeriveStats can report the mean
    /// of per-sample reductions rather than a value algebraically identical to
    /// the aggregate total. Samples with no toothpicks contribute 0%.
    if (toothpicks > 0)
      AtomicAdd(
        s.toothpicksReductionSum,
        100.0 * static_cast<double>(toothpicks - convertedToothpicks) / static_cast<double>(toothpicks)
      );

    if (unmatched > 0)
      AtomicAdd(s.withNonCompliance, 1.0);
    AtomicAdd(s.nonComplianceCount, static_cast<double>(unmatched));
    if (AtomicMax(s.nonComplianceMax, static_cast<double>(unmatched)))
      s.stringMaxNonCompliance.set(string);

    if (maxDepth > 1)
      AtomicAdd(s.withNesting, 1.0);
    AtomicAdd(s.nestingDepthTotal, static_cast<double>(maxDepth));
    if (AtomicMax(s.nestingDepthMax, static_cast<double>(maxDepth)))
      s.stringMaxNested.set(string);

    if (maxValidDepth > 1)
      AtomicAdd(s.withValidNesting, 1.0);
    AtomicAdd(s.validNestingDepthTotal, static_cast<double>(maxValidDepth));
    if (AtomicMax(s.validNestingDepthMax, static_cast<double>(maxValidDepth)))
      s.stringMaxValidNested.set(string);
  };

  applyEmbedded(stats);
  if (extraEmbedded)
    applyEmbedded(*extraEmbedded);

  return unmatched;
}
