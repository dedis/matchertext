#include <filesystem>
#include <iostream>
#include <set>

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

int main(const int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <file|directory>...\n";
    return -1;
  }

  // Parse arguments
  bool logStrings = false;
  std::set<std::string> filesToProcess;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--log-strings") {
      logStrings = true;
      continue;
    }
    fs::path p(arg);
    if (!fs::exists(p)) {
      std::cerr << "Path does not exist: " << p << "\n";
      continue;
    }
    if (fs::is_regular_file(p) && is_c_cpp_file(p))
      filesToProcess.insert(normalize_path(p.string()));
    else if (fs::is_directory(p))
      for (const auto &entry: fs::recursive_directory_iterator(p))
        if (fs::is_regular_file(entry) && is_c_cpp_file(entry.path().string()))
          filesToProcess.insert(normalize_path(entry.path().string()));
  }

  try {
    const std::vector files(filesToProcess.begin(), filesToProcess.end());
    const auto start = std::chrono::high_resolution_clock::now();

    #pragma omp parallel for schedule(dynamic) default(none) shared(files)
    for (const auto &file: files) {
      Parser::ParseFile(file);
    }

    Parser::STRING_STATS.DeriveStats();
    Parser::DOCS_STATS.DeriveStats();
    Parser::DOCS_RELAXED_STATS.DeriveStats();

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    PrintStatsTable(
      {
        {"Strings", SnapshotStats(Parser::STRING_STATS)},
        {"Documentation", SnapshotStats(Parser::DOCS_STATS)},
        {"Documentation Relaxed", SnapshotStats(Parser::DOCS_RELAXED_STATS)},
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
