#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <ranges>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "include/LanguageData.hpp"
#include "include/LanguageParser.hpp"
#include "include/Parser.hpp"
#include "include/Stats.hpp"

#if USE_OPENMP
#include <omp.h>
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static const char *kUsage =
    "Matchertext Analyzer\n"
    "\n"
    "USAGE:\n"
    "    %s <file|directory>... [OPTIONS]\n"
    "\n"
    "ARGS:\n"
    "    <file|directory>...    One or more source files or directories to analyze\n"
    "\n"
    "OPTIONS:\n"
    "    --language <lang>              Only analyze files of the given language\n"
    "    --output <name>                Write results to ./result/<name> (default: ./result/<input-path>)\n"
    "    --extensions <ext1,ext2,...>   Comma-separated list of additional file extensions\n"
    "\n"
    "EXAMPLES:\n"
    "    %[1]s ./my_project\n"
    "    %[1]s ./src --language cpp\n"
    "    %[1]s ./src --language cpp --extensions cp+,hp+\n";

static long long elapsed_ms(const Clock::time_point start, const Clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

// Index of the executing OpenMP thread (0 when built without OpenMP). Used to
// route work into per-thread storage that needs no locking.
static int current_thread() {
#if USE_OPENMP
  return omp_get_thread_num();
#else
  return 0;
#endif
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

constexpr bool same_language_family(const LanguageEnum a, const LanguageEnum b) {
  if (a == b)
    return true;
  const bool aIsC = a == LanguageEnum::C || a == LanguageEnum::CPP;
  const bool bIsC = b == LanguageEnum::C || b == LanguageEnum::CPP;
  return aIsC && bIsC;
}

static std::vector<std::string_view> split_comma(std::string_view s) {
  std::vector<std::string_view> out;
  size_t start = 0;
  while (true) {
    const size_t pos = s.find(',', start);
    if (pos == std::string_view::npos) {
      out.emplace_back(s.substr(start));
      break;
    }
    out.emplace_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

static std::string list_known_languages() {
  std::string out;
  for (const auto &data: kLanguageData | std::views::values) {
    if (data.alias.empty())
      continue;
    if (!out.empty())
      out += ", ";
    out += data.alias[0];
  }
  return out;
}

int main(const int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, kUsage, argv[0]);
    return -1;
  }

  log_info("Starting parser"); {
    std::ostringstream msg;
#if USE_OPENMP
    const char *envThreads = std::getenv("OMP_NUM_THREADS");
    if (!envThreads)
      omp_set_num_threads(omp_get_num_procs() * 2);
    const int maxThreads = omp_get_max_threads();
    msg << "OpenMP enabled: max_threads=" << maxThreads
        << " (OMP_NUM_THREADS=" << (envThreads ? envThreads : "<unset, defaulting to 2x cores>") << ")";
#else
    msg << "OpenMP disabled at build time (single-threaded parsing)";
#endif
    log_info(msg.str());
  }

  std::string outputName; // when set via --output, names the subdir under ./result
  auto filterLanguage = LanguageEnum::Unknown;
  std::vector<std::string_view> extraExtensions;
  std::vector<std::string> rawPaths;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--output") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--output requires a value\n\n");
        std::fprintf(stderr, kUsage, argv[0]);
        return -1;
      }
      outputName = argv[++i];
      continue;
    }
    if (arg == "--extensions") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--extensions requires a value\n\n");
        std::fprintf(stderr, kUsage, argv[0]);
        return -1;
      }
      extraExtensions = split_comma(argv[++i]);
      continue;
    }
    if (arg == "--language") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--language requires a value\n\n");
        std::fprintf(stderr, kUsage, argv[0]);
        return -1;
      }
      if (!LanguageParser::ParseLanguage(argv[++i], filterLanguage)) {
        std::fprintf(stderr, "Unknown language: %s (known: %s)\n\n", argv[i], list_known_languages().c_str());
        std::fprintf(stderr, kUsage, argv[0]);
        return -1;
      }
      continue;
    }
    rawPaths.push_back(arg);
  }

  if (rawPaths.empty()) {
    std::fprintf(stderr, "Missing required <file|directory> argument\n\n");
    std::fprintf(stderr, kUsage, argv[0]);
    return -1;
  }

  const bool singleLanguage = filterLanguage != LanguageEnum::Unknown;
  if (!singleLanguage)
    extraExtensions.clear();

  std::vector<std::string> inputPaths;
  inputPaths.reserve(rawPaths.size());
  for (const auto &p: rawPaths)
    inputPaths.push_back(fs::path(p).lexically_normal().string());

  // Output goes under ./result. With --output <name> the subdir is exactly
  // <name>; otherwise it is derived from the first input path (relative paths
  // as-is, absolute paths by their last component).
  std::string outputDir;
  {
    fs::path sub;
    if (!outputName.empty())
      sub = fs::path(outputName);
    else {
      const fs::path ip = fs::path(rawPaths[0]).lexically_normal();
      sub = ip.is_relative() ? ip : ip.filename();
    }
    outputDir = (fs::path("./result") / sub).lexically_normal().string();
  }

  std::error_code ec;
  fs::create_directories(outputDir, ec);
  if (ec) {
    std::cerr << "Failed to create output directory '" << outputDir << "': " << ec.message() << '\n';
    return -1;
  }

  Parser::ConfigureDebugLanguages(inputPaths, outputDir);

  // Build extension→language lookup once so the walk is O(1) per file.
  // In single-language mode expand to the whole language family + extra extensions.
  std::unordered_map<std::string, LanguageEnum> extToLang;
  if (singleLanguage) {
    for (const auto &[lang, data]: kLanguageData) {
      if (!same_language_family(lang, filterLanguage))
        continue;
      for (const auto &ext: data.extensions)
        extToLang.try_emplace(std::string(ext), filterLanguage);
    }
    for (const auto &ext: extraExtensions)
      extToLang.try_emplace(std::string(ext), filterLanguage);
  } else {
    for (const auto &[lang, data]: kLanguageData)
      for (const auto &ext: data.extensions)
        extToLang.try_emplace(std::string(ext), lang);
  }

  // Parallel directory walk: bucket files by language.
  // The readdir/stat syscalls and the per-file work (extension parsing plus
  // lexically_normal, which is pure string arithmetic) are spread across
  // threads via OpenMP tasks — one task per subdirectory, so sibling subtrees
  // are walked concurrently. Each thread fills its own bucket set, so the hot
  // path is lock-free; a cheap serial merge below deduplicates and assembles
  // the global buckets.
  const auto indexingStart = Clock::now();
  std::unordered_set<std::string> seen;
  std::map<LanguageEnum, std::vector<std::pair<std::string, std::string>>> buckets;

#if USE_OPENMP
  const int indexThreads = omp_get_max_threads();
#else
  const int indexThreads = 1;
#endif
  std::vector<std::map<LanguageEnum, std::vector<std::pair<std::string, std::string>>>> tlBuckets(indexThreads);

  // Resolve roots up front so their normalized input-path strings live in
  // stable storage that every spawned task can safely reference.
  std::vector<std::pair<fs::path, std::string>> roots;
  roots.reserve(rawPaths.size());
  for (const auto &rawPath: rawPaths) {
    const fs::path p(rawPath);
    if (!fs::exists(p)) {
      std::cerr << "Path does not exist: " << p << "\n";
      continue;
    }
    roots.emplace_back(p, p.lexically_normal().string());
  }

  // Match a candidate file against the extension table and stash it in the
  // current thread's bucket. Deduplication is deferred to the serial merge, so
  // this touches only thread-local state and needs no synchronization.
  auto try_add = [&](const fs::path &fp, const std::string &inputPath) {
    const std::string extStr = fp.extension().string();
    if (extStr.size() <= 1)
      return;
    const auto it = extToLang.find(extStr.substr(1));
    if (it == extToLang.end())
      return;
    tlBuckets[current_thread()][it->second].emplace_back(fp.lexically_normal().string(), inputPath);
  };

  // Non-recursive scan of one directory; each subdirectory is handed to its own
  // task. is_directory/is_regular_file consume the file type cached by readdir
  // where the platform provides it, avoiding extra stat calls. Errors (e.g.
  // permission denied) skip the offending entry rather than aborting the walk.
  std::function<void(const fs::path &, const std::string &)> walk =
      [&](const fs::path &dir, const std::string &inputPath) {
    std::vector<fs::path> subdirs;
    std::error_code wec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, wec), end;
         !wec && it != end; it.increment(wec)) {
      std::error_code tec;
      if (it->is_directory(tec))
        subdirs.push_back(it->path());
      else if (it->is_regular_file(tec))
        try_add(it->path(), inputPath);
    }
    for (auto &sd: subdirs) {
#if USE_OPENMP
#pragma omp task firstprivate(sd) shared(walk, inputPath)
#endif
      walk(sd, inputPath);
    }
  };

#if USE_OPENMP
#pragma omp parallel
#pragma omp single
#endif
  for (const auto &[p, inputPath]: roots) {
    if (fs::is_regular_file(p))
      try_add(p, inputPath);
    else if (fs::is_directory(p))
      walk(p, inputPath);
  }

  // Serial merge: deduplicate across threads and assemble the global buckets,
  // then sort each bucket so output is reproducible regardless of the order in
  // which threads happened to discover files.
  for (auto &tb: tlBuckets)
    for (auto &[lang, files]: tb)
      for (auto &entry: files)
        if (seen.insert(entry.first).second)
          buckets[lang].push_back(std::move(entry));
  for (auto &files: buckets | std::views::values)
    std::ranges::sort(files);

  // Assemble langFiles in kLanguageData order for deterministic output.
  std::vector<std::pair<LanguageEnum, std::vector<std::pair<std::string, std::string>>>> langFiles;
  size_t totalFiles = 0;
  for (const auto &lang: kLanguageData | std::views::keys) {
    if (singleLanguage && lang != filterLanguage)
      continue;
    auto it = buckets.find(lang);
    if (it == buckets.end() || it->second.empty())
      continue;
    totalFiles += it->second.size();
    langFiles.emplace_back(lang, std::move(it->second));
  }

  const long long indexingMs = elapsed_ms(indexingStart, Clock::now()); {
    std::ostringstream msg;
    msg << "Indexed " << pluralize(totalFiles, "file", "files")
        << " across " << pluralize(langFiles.size(), "language", "languages");
    log_info(msg.str());
  }

  if (langFiles.empty()) {
    log_info("No matching source files found, exiting");
    return 0;
  }

  std::map<LanguageEnum, std::unique_ptr<PerLanguageStats>> perLangStats;
  for (const auto &lang: langFiles | std::views::keys)
    perLangStats.emplace(lang, std::make_unique<PerLanguageStats>());

  try {
    const auto parseStart = Clock::now();
    std::vector<std::tuple<LanguageEnum, size_t, long long>> perLangTiming;
    perLangTiming.reserve(langFiles.size());

    for (auto &lang: langFiles) {
      {
        std::ostringstream msg;
        msg << "Parsing " << pluralize(lang.second.size(), "file", "files") << " [" << LanguageName(lang.first) << "]";
        log_info(msg.str());
      }

      PerLanguageStats *pls = perLangStats.at(lang.first).get();

      std::atomic<size_t> done{0};
      std::atomic<int> displayedPct{-1};
      const size_t langTotal = lang.second.size();
      const auto langStart = Clock::now();

      #if USE_OPENMP
      #pragma omp parallel for schedule(dynamic) default(none) shared(lang, pls, done, displayedPct, langTotal)
      #endif
      for (const auto &[filePath, inputPath]: lang.second) {
        try {
          if (Serde::JSON result; LanguageParser::ExtractData(lang.first, filePath, result))
            Parser::GatherStatistics(std::move(result), filePath, inputPath, pls);
        } catch (const std::exception &e) {
          #pragma omp critical
          {
            std::cerr << "\nFAILED " << filePath << ": " << e.what() << '\n';
          }
        }

        const size_t n = ++done;
        if (const int newPct = static_cast<int>(n * 100 / langTotal); newPct > displayedPct.load(std::memory_order_relaxed)) {
          #pragma omp critical
          {
            if (const int cur = displayedPct.load(); newPct > cur) {
              displayedPct.store(newPct);
              constexpr int W = 40;
              const int filled = newPct * W / 100;
              std::cerr << "\r  [";
              for (int i = 0; i < W; ++i)
                std::cerr << (i < filled ? '#' : '.');
              std::cerr << "] " << n << "/" << langTotal << " (" << newPct << "%)   " << std::flush;
            }
          }
        }
      }
      std::cerr << '\n';
      perLangTiming.emplace_back(lang.first, langTotal, elapsed_ms(langStart, Clock::now()));
    }

    const long long parsingMs = elapsed_ms(parseStart, Clock::now());

    Parser::FlushDebugLanguageLogs();

    auto open_stat_file = [&](const std::string &name) -> std::ofstream {
      const fs::path p = fs::path(outputDir) / name;
      std::ofstream f(p, std::ios::trunc);
      if (!f)
        throw std::runtime_error("Failed to open output file '" + p.string() + "'");
      return f;
    }; {
      auto f = open_stat_file("strings.md");
      f << "# Embedded String Statistics\n\n";
      PrintStatsTable(
        {
          {"Strings", Parser::STRING_STATS.Snapshot()},
          {"Documentation", Parser::DOCS_STATS.Snapshot()},
          {"Documentation Relaxed", Parser::DOCS_RELAXED_STATS.Snapshot()},
        }, f
      );
    } {
      auto f = open_stat_file("files.md");
      f << "# File Statistics\n\n";
      PrintFileStatsTable(Parser::FILE_STATS.Snapshot(), f);
    } {
      auto f = open_stat_file("nesting.md");
      f << "# Nesting Statistics\n\n";
      PrintNestedStatsTable(
        {
          {"Strings", Parser::STRING_NESTED_STATS.Snapshot()},
          {"Documentation", Parser::DOCS_NESTED_STATS.Snapshot()},
          {"Documentation Relaxed", Parser::DOCS_RELAXED_NESTED_STATS.Snapshot()},
        }, f
      );
    } {
      auto f = open_stat_file("language_stats.md");
      f << "# String Language Distribution\n\n";
      PrintLanguageStatsTable({{"String", Parser::STRING_LANG_STATS.Snapshot()}}, f);
    }

    for (const auto &[lang, pls]: perLangStats) {
      const fs::path langDir = fs::path(outputDir) / "language_stats" / LanguageName(lang);
      std::error_code langEc;
      fs::create_directories(langDir, langEc);
      if (langEc)
        throw std::runtime_error(
          "Failed to create language stats dir '" + langDir.string() + "': " + langEc.message()
        );

      auto open_lang_file = [&](const std::string &name) -> std::ofstream {
        const fs::path p = langDir / name;
        std::ofstream f(p, std::ios::trunc);
        if (!f)
          throw std::runtime_error("Failed to open output file '" + p.string() + "'");
        return f;
      }; {
        auto f = open_lang_file("strings.md");
        f << "# Embedded String Statistics\n\n";
        PrintStatsTable(
          {
            {"Strings", pls->stringStats.Snapshot()},
            {"Documentation", pls->docsStats.Snapshot()},
            {"Documentation Relaxed", pls->docsRelaxedStats.Snapshot()},
          }, f
        );
      } {
        auto f = open_lang_file("files.md");
        f << "# File Statistics\n\n";
        PrintFileStatsTable(pls->fileStats.Snapshot(), f);
      } {
        auto f = open_lang_file("nesting.md");
        f << "# Nesting Statistics\n\n";
        PrintNestedStatsTable(
          {
            {"Strings", pls->stringNestedStats.Snapshot()},
            {"Documentation", pls->docsNestedStats.Snapshot()},
            {"Documentation Relaxed", pls->docsRelaxedNestedStats.Snapshot()},
          }, f
        );
      } {
        auto f = open_lang_file("language_stats.md");
        f << "# String Language Distribution\n\n";
        PrintLanguageStatsTable({{"String", pls->langStats.Snapshot()}}, f);
      }
    }

    std::ostringstream msg;
    msg << "\nTiming summary"
        << "\n - Indexing: " << indexingMs << " ms"
        << "\n - Parsing : " << parsingMs << " ms";
    for (const auto &[lang, count, ms]: perLangTiming) {
      const double secs = static_cast<double>(ms) / 1000.0;
      const double rate = secs > 0 ? static_cast<double>(count) / secs : 0.0;
      msg << "\n   - " << LanguageName(lang) << ": " << ms << " ms ("
          << count << " files, " << std::fixed << std::setprecision(1) << rate << " files/s)";
      msg.unsetf(std::ios::fixed);
    }
    msg << "\nResults written to: " << fs::path(outputDir).lexically_normal().string();
    log_info(msg.str());
  } catch (const std::exception &e) {
    std::cerr << "Parsing failed: " << e.what() << "\n";
    return -1;
  }

  return 0;
}
