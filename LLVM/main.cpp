#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "include/LanguageParser.hpp"
#include "include/Parser.hpp"
#include "include/Stats.hpp"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static long long elapsed_ms(const Clock::time_point start, const Clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

static void log_info(const std::string &message) {
  std::istringstream lines(message);
  std::string line;
  while (std::getline(lines, line))
    std::cerr << "[matchertext] " << line << '\n';
}

static std::string pluralize(const size_t count, const std::string_view singular, const std::string_view plural) {
  return std::to_string(count) + " " + std::string(count == 1 ? singular : plural);
}

static std::string normalize_path(const std::string &in) {
  try {
    return fs::weakly_canonical(fs::path(in)).lexically_normal().string();
  } catch (...) {
    return fs::path(in).lexically_normal().string();
  }
}

/// File extensions recognized per language. C and C++ share the indexing pool
/// because headers like `.h` are ambiguous and the underlying parser handles both.
static const std::unordered_map<Language, std::vector<std::string_view>> kLanguageExtensions = {
  {Language::C,      {"c", "h"}},
  {Language::CPP,    {"cc", "cpp", "cxx", "hpp", "hh", "hxx"}},
  {Language::Go,     {"go"}},
  {Language::Python, {"py", "pyw", "pyi", "pyz", "pyzw"}},
};

/// True when `a` and `b` are the same language family for indexing purposes.
constexpr bool same_language_family(const Language a, const Language b) {
  if (a == b)
    return true;
  const bool aIsCFamily = a == Language::C || a == Language::CPP;
  const bool bIsCFamily = b == Language::C || b == Language::CPP;
  return aIsCFamily && bIsCFamily;
}

/// Return true if `path` has an extension belonging to `language` (or its family).
inline bool matches_language(const std::string &path, const Language language) {
  const auto pos = path.rfind('.');
  if (pos == std::string::npos)
    return false;

  const std::string_view ext(path.data() + pos + 1, path.size() - pos - 1);
  for (const auto &[lang, extensions]: kLanguageExtensions) {
    if (!same_language_family(lang, language))
      continue;
    for (const auto &e: extensions)
      if (e == ext)
        return true;
  }
  return false;
}

int main(const int argc, char *argv[]) {
  long long indexingMs = 0;
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0]
        << " <language> [--log-strings] [--debug-languages] [--compiler <compiler>] <file|directory>...\n";
    return -1;
  }

  log_info("Starting parser");

  // Parse arguments
  bool logStrings = false;
  bool debugLanguages = false;
  std::string compilerOverride;
  auto language = Language::Unknown;
  std::map<std::string, std::string> filesToProcess;
  std::vector<std::string> inputPaths;
  const auto indexingStart = Clock::now();
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--log-strings") {
      logStrings = true;
      continue;
    }
    if (arg == "--debug-languages") {
      debugLanguages = true;
      continue;
    }
    if (arg == "--compiler") {
      if (i + 1 >= argc) {
        std::cerr << "--compiler requires a value\n";
        return -1;
      }
      compilerOverride = argv[++i];
      continue;
    }

    if (language == Language::Unknown) {
      Language parsed;
      if (!LanguageParser::ParseLanguage(arg, parsed)) {
        std::cerr << "Unknown language: " << arg
            << " (expected one of: c, cpp, go, python)\n";
        return -1;
      }
      language = parsed;
      continue;
    }

    fs::path p(arg);
    if (!fs::exists(p)) {
      std::cerr << "Path does not exist: " << p << "\n";
      continue;
    }

    const std::string inputPath = fs::path(arg).lexically_normal().string();
    inputPaths.push_back(inputPath);
    if (fs::is_regular_file(p) && matches_language(p.string(), language))
      filesToProcess.try_emplace(normalize_path(p.string()), inputPath);
    else if (fs::is_directory(p)) {
      for (const auto &entry: fs::recursive_directory_iterator(p))
        if (fs::is_regular_file(entry) && matches_language(entry.path().string(), language))
          filesToProcess.try_emplace(normalize_path(entry.path().string()), inputPath);
    }
  }

  if (language == Language::Unknown) {
    std::cerr << "Missing required <language> argument\n";
    return -1;
  }
  const auto indexingEnd = Clock::now();
  indexingMs = elapsed_ms(indexingStart, indexingEnd);
  {
    std::ostringstream message;
    message << "Indexed " << pluralize(filesToProcess.size(), "file", "files")
        << " from " << pluralize(inputPaths.size(), "input path", "input paths");
    log_info(message.str());
  }

  if (filesToProcess.empty()) {
    log_info("No matching source files found, exiting");
    return 0;
  }

  try {
    long long parsingMs = 0;
    std::vector<std::pair<std::string, std::string>> files;
    files.reserve(filesToProcess.size());
    for (const auto &[filePath, inputPath]: filesToProcess)
      files.emplace_back(filePath, inputPath);

    if (debugLanguages) {
      log_info("Debug language sampling enabled");
      Parser::ConfigureDebugLanguages(inputPaths);
    }

    std::ostringstream message;
    message << "Parsing " << pluralize(files.size(), "file", "files");
    if (logStrings)
      message << " with string logging enabled";
    log_info(message.str());

    const auto parseStart = Clock::now();

    #if USE_OPENMP
    #pragma omp parallel for schedule(dynamic) default(none) shared(files, language, compilerOverride)
    #endif
    for (const auto &[filePath, inputPath]: files) {
      try {
        if (Serde::JSON result; LanguageParser::ExtractData(language, compilerOverride, filePath, result))
          Parser::GatherStatistics(std::move(result), filePath, inputPath);
      } catch (const std::exception &e) {
        #pragma omp critical
        std::cerr << "FAILED " << filePath << ": " << e.what() << '\n';
        throw;
      }
    }

    const auto parseEnd = Clock::now();
    parsingMs = elapsed_ms(parseStart, parseEnd);

    if (debugLanguages) {
      Parser::FlushDebugLanguageLogs();
    }

    PrintStatsTable(
      {
        {"Strings", Parser::STRING_STATS.Snapshot()},
        {"Documentation", Parser::DOCS_STATS.Snapshot()},
        {"Documentation Relaxed", Parser::DOCS_RELAXED_STATS.Snapshot()},
      }
    );

    std::cout << "\n\n\n";
    PrintFileStatsTable(Parser::FILE_STATS.Snapshot());

    PrintNestedStatsTable(
      {
        {"Strings", Parser::STRING_NESTED_STATS.Snapshot()},
        {"Documentation", Parser::DOCS_NESTED_STATS.Snapshot()},
        {"Documentation Relaxed", Parser::DOCS_RELAXED_NESTED_STATS.Snapshot()},
      }
    );

    PrintLanguageStatsTable(
      {
        {"String", Parser::STRING_LANG_STATS.Snapshot()},
      }
    );

    if (logStrings) {
      std::cout << "\n\n";
      PrintStatsMaxString(Parser::STRING_STATS, Parser::DOCS_STATS);
    }

    message.clear();
    std::cout.put('\n');
    message << "Timing summary"
            << "\n - Indexing: " << indexingMs << " ms"
            << "\n - Parsing : " << parsingMs << " ms";
    log_info(message.str());
  } catch (const std::exception &e) {
    std::cerr << "Parsing failed: " << e.what() << "\n";
    return -1;
  }

  return 0;
}

// Most common violation in files
