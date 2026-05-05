//
// Parser.cpp
// Author: Antoine Bastide
// Date: 13/06/2025
//

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

  void EnsureDirectoryPrepared(const fs::path &directory, const bool clearExisting) {
    std::error_code ec;
    if (clearExisting)
      fs::remove_all(directory, ec);
    if (ec)
      throw std::runtime_error(
        "Failed to clear debug language directory '" +
        directory.string() + "': " + ec.message()
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
        EnsureDirectoryPrepared(root_, false);
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
      ) {
        {
          std::lock_guard lock(mutex_);
          if (!enabled_) return;
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
        if (!enabled_) return;

        // Clear stale .txt files from a previous run.
        std::error_code ec;
        for (const auto &entry: fs::directory_iterator(root_, ec)) {
          if (ec) break;
          if (!entry.is_regular_file(ec) || ec) continue;
          if (entry.path().extension() != ".txt") continue;
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
          if (bucket.seen == 0 || bucket.samples.empty()) continue;

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
static std::string ExtractLiteralBody(std::string_view spelling) {
  const size_t quote = spelling.find('"');
  if (quote == std::string_view::npos)
    return {};

  if (const bool isRaw = quote > 0 && spelling[quote - 1] == 'R'; !isRaw) {
    const size_t end = spelling.rfind('"');
    if (end == std::string_view::npos || end <= quote)
      return {};
    return std::string(spelling.substr(quote + 1, end - quote - 1));
  }

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

void Parser::ParseFile(const std::string &filePath, const std::string &inputPath) {
  if (JSON result; ParseC_CPP(filePath, result) && result.IsArray())
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

bool Parser::ParseC_CPP(const std::string &path, JSON &result) {
  /// Static compiler infrastructure reused across calls to avoid repeated setup cost.
  static clang::DiagnosticOptions diagOpts;
  static auto diagIDs = llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>();
  static clang::IgnoringDiagConsumer diagConsumer;
  static clang::DiagnosticsEngine diags(diagIDs, diagOpts, &diagConsumer, false);

  /// Language configuration for the lexer.
  static clang::LangOptions langOpts;
  static bool langInitialized = false;
  if (!langInitialized) {
    langOpts.CPlusPlus = true;
    langOpts.CPlusPlus20 = true;
    langInitialized = true;
  }

  /// Shared file manager reused for all parsed files.
  static clang::FileSystemOptions fsOpts;
  static clang::FileManager fileMgr(fsOpts);

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
    result = JSON::Array();

  /// Tokenize the file until EOF.
  clang::Token tok{};
  while (true) {
    lexer.LexFromRawLexer(tok);
    if (tok.is(clang::tok::eof))
      break;

    /// Handle string literal tokens.
    if (IsStringToken(tok)) {
      std::string value;
      clang::Token current = tok;

      /// Concatenate adjacent string literals ("a" "b").
      do {
        const std::string spelling = clang::Lexer::getSpelling(current, srcMgr, langOpts);
        value += ExtractLiteralBody(spelling);
        lexer.LexFromRawLexer(current);
      } while (IsStringToken(current));

      tok = current;
      result.PushBack(JSON::Object({{"kind", JSON("string")}, {"value", JSON(value)}}));
      continue;
    }

    /// Capture comment tokens.
    if (tok.is(clang::tok::comment)) {
      std::string comment = clang::Lexer::getSpelling(tok, srcMgr, langOpts);
      result.PushBack(JSON::Object({{"kind", JSON("comment")}, {"value", JSON(comment)}}));
    }
  }

  return true;
}

void Parser::GatherStatistics(JSON &&json, const std::string &path, const std::string_view inputPath) {
  uint64_t fileViolations = 0;
  uint64_t fileViolationsRelaxed = 0;

  for (auto e: json.GetArray()) {
    std::string value = e["value"].GetString();
    if (e["kind"].GetString() == "string") {
      const uint64_t violations = process(
        std::move(value), STRING_STATS, STRING_NESTED_STATS, &STRING_LANG_STATS, false, path,
        inputPath.empty() ? std::string_view(path) : inputPath
      );
      fileViolations += violations;
      fileViolationsRelaxed += violations;
    } else {
      fileViolations += process(std::string(value), DOCS_STATS, DOCS_NESTED_STATS);
      fileViolationsRelaxed += process(std::move(value), DOCS_RELAXED_STATS, DOCS_RELAXED_NESTED_STATS, nullptr, true);
    }
  }

  AtomicAdd(FILE_STATS.count, 1.0);

  if (fileViolations > 0)
    AtomicAdd(FILE_STATS.withViolation, 1.0);
  AtomicAdd(FILE_STATS.violationCount, static_cast<double>(fileViolations));
  AtomicMax(FILE_STATS.violationMax, static_cast<double>(fileViolations));

  if (fileViolationsRelaxed > 0)
    AtomicAdd(FILE_STATS.withViolationRelaxed, 1.0);
  AtomicAdd(FILE_STATS.violationCountRelaxed, static_cast<double>(fileViolationsRelaxed));
  AtomicMax(FILE_STATS.violationMaxRelaxed, static_cast<double>(fileViolationsRelaxed));
}

uint64_t Parser::process(
  std::string &&string, EmbeddedStats &stats, NestedStats &nestedStats,
  LanguageStats *langStats, const bool relaxed,
  const std::string_view sourcePath,
  const std::string_view inputPath
) {
  uint64_t toothpicks = 0;
  for (const unsigned char c: string) {
    if (c == '\\')
      ++toothpicks;
  }

  const auto [unmatched, maxDepth, maxValidDepth, rawChars] = AnalyzeMatcherText(string, relaxed);

  if (langStats != nullptr) {
    /// Classify the embedded language and record per-language stats only for strings.
    if (const auto [lang, confidence] = ClassifyString(string); lang != LanguageEnum::Unknown) {
      langStats->Record(lang, unmatched, toothpicks);
      GetDebugLanguageLogger().Record(
        inputPath, lang, sourcePath, string,
        confidence, unmatched, toothpicks
      );
    }
  }

  nestedStats.Record(maxDepth, maxValidDepth);

  AtomicAdd(stats.count, 1.0);
  AtomicAdd(stats.rawChars, static_cast<double>(rawChars));

  if (toothpicks > 0)
    AtomicAdd(stats.withToothpicks, 1.0);
  AtomicAdd(stats.toothpicks, static_cast<double>(toothpicks));
  if (AtomicMax(stats.toothpicksMax, static_cast<double>(toothpicks)))
    stats.stringMaxToothpicks.set(string);

  if (unmatched > 0)
    AtomicAdd(stats.withNonCompliance, 1.0);
  AtomicAdd(stats.nonComplianceCount, static_cast<double>(unmatched));
  if (AtomicMax(stats.nonComplianceMax, static_cast<double>(unmatched)))
    stats.stringMaxNonCompliance.set(string);

  if (maxDepth > 1)
    AtomicAdd(stats.withNesting, 1.0);
  AtomicAdd(stats.nestingDepthTotal, static_cast<double>(maxDepth));
  if (AtomicMax(stats.nestingDepthMax, static_cast<double>(maxDepth)))
    stats.stringMaxNested.set(string);

  if (maxValidDepth > 1)
    AtomicAdd(stats.withValidNesting, 1.0);
  AtomicAdd(stats.validNestingDepthTotal, static_cast<double>(maxValidDepth));
  if (AtomicMax(stats.validNestingDepthMax, static_cast<double>(maxValidDepth)))
    stats.stringMaxValidNested.set(string);

  return unmatched;
}
