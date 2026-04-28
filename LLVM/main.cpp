#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

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

/// Return true if the path has a C/C++ source or header extension.
inline bool is_c_cpp_file(const std::string &path) {
  const auto pos = path.rfind('.');
  if (pos == std::string::npos)
    return false;

  const std::string ext = path.substr(pos + 1);
  return ext == "c" || ext == "h" || ext == "cc" || ext == "cpp" || ext == "cxx" || ext == "hpp" || ext == "hh" ||
         ext == "hxx";
}

struct WorkItem {
  std::string filePath;
  std::string inputPath;
};

int main(const int argc, char *argv[]) {
  long long indexingMs = 0;
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0]
        << " [--log-strings] [--debug-languages] <file|directory>...\n";
    return -1;
  }

  log_info("Starting parser");

  // Parse arguments
  bool logStrings = false;
  bool debugLanguages = false;
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

    fs::path p(arg);
    if (!fs::exists(p)) {
      std::cerr << "Path does not exist: " << p << "\n";
      continue;
    }

    const std::string inputPath = fs::path(arg).lexically_normal().string();
    inputPaths.push_back(inputPath);
    if (fs::is_regular_file(p) && is_c_cpp_file(p))
      filesToProcess.try_emplace(normalize_path(p.string()), inputPath);
    else if (fs::is_directory(p)) {
      for (const auto &entry: fs::recursive_directory_iterator(p))
        if (fs::is_regular_file(entry) && is_c_cpp_file(entry.path().string()))
          filesToProcess.try_emplace(normalize_path(entry.path().string()), inputPath);
    }
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
    log_info("No matching C/C++ files found, exiting");
    return 0;
  }

  try {
    long long parsingMs = 0;
    std::vector<WorkItem> files;
    files.reserve(filesToProcess.size());
    for (const auto &[filePath, inputPath]: filesToProcess)
      files.push_back({filePath, inputPath});

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
    #pragma omp parallel for schedule(dynamic) default(none) shared(files)
    #endif
    for (const auto &file: files) {
      Parser::ParseFile(file.filePath, file.inputPath);
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

    std::ostringstream message;
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
