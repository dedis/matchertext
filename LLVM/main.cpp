#include <filesystem>
#include <iostream>
#include <map>
#include <vector>

#include "include/Parser.hpp"
#include "include/Stats.hpp"

namespace fs = std::filesystem;

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
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0]
              << " [--log-strings] [--debug-languages] <file|directory>...\n";
    return -1;
  }

  // Parse arguments
  bool logStrings = false;
  bool debugLanguages = false;
  std::map<std::string, std::string> filesToProcess;
  std::vector<std::string> inputPaths;
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

  try {
    std::vector<WorkItem> files;
    files.reserve(filesToProcess.size());
    for (const auto &[filePath, inputPath] : filesToProcess)
      files.push_back({filePath, inputPath});

    if (debugLanguages)
      Parser::ConfigureDebugLanguages(inputPaths);

    const auto start = std::chrono::high_resolution_clock::now();

    #if USE_OPENMP
    #pragma omp parallel for schedule(dynamic) default(none) shared(files)
    #endif
    for (const auto &file: files) {
      Parser::ParseFile(file.filePath, file.inputPath);
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (debugLanguages)
      Parser::FlushDebugLanguageLogs();

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

    std::cout << "\nParsing took " << duration << " ms\n";
  } catch (const std::exception &e) {
    std::cerr << "Parsing failed: " << e.what() << "\n";
    return -1;
  }

  return 0;
}

// Most common violation in files
