#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Parser.hpp"

namespace fs = std::filesystem;

namespace {
bool ExpectContains(const fs::path &path, const std::string_view needle) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "debug_language_output_test failed: missing file " << path << '\n';
    return false;
  }

  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();
  if (text.find(needle) != std::string::npos)
    return true;

  std::cerr << "debug_language_output_test failed: '" << needle
            << "' not found in " << path << '\n';
  return false;
}
} // namespace

int main(const int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: debug_language_output_test <fixture.cpp> <output-dir>\n";
    return 2;
  }

  const fs::path fixturePath = fs::path(argv[1]);
  const fs::path outputRoot = fs::path(argv[2]);
  const std::string inputPath = "tests/string_bucket_mix.cpp";

  std::error_code ec;
  fs::remove_all(outputRoot, ec);
  if (ec) {
    std::cerr << "debug_language_output_test failed: could not clear " << outputRoot
              << ": " << ec.message() << '\n';
    return 1;
  }

  if (setenv("MATCHERTEXT_DEBUG_LANGUAGE_DIR", outputRoot.c_str(), 1) != 0) {
    std::cerr << "debug_language_output_test failed: setenv failed\n";
    return 1;
  }

  Parser::ConfigureDebugLanguages({inputPath});
  if (Serde::JSON result; Parser::ParseC_CPP(fixturePath.string(), result))
    Parser::GatherStatistics(std::move(result), fixturePath.string(), inputPath);
  Parser::FlushDebugLanguageLogs();

  const fs::path base = outputRoot / "tests" / "string_bucket_mix.cpp";
  bool ok = true;
  ok &= ExpectContains(base / "FilePath.txt", "# SampleCount: 1");
  ok &= ExpectContains(base / "FilePath.txt", "../config/settings.yaml");
  ok &= ExpectContains(base / "FormatString.txt", "status=%s code=%d");
  ok &= ExpectContains(base / "Shell.txt",
                       "grep -R \"needle\" src | sed 's/foo/bar/' > out.txt");
  ok &= ExpectContains(base / "YAML.txt",
                       "service: matchertext\\nretries: 3\\npaths:\\n  - src");
  ok &= ExpectContains(base / "HexData.txt", "4d5a90000300000004000000ffff0000");
  ok &= ExpectContains(base / "PseudoBinaryData.txt",
                       "\\\\x89PNG\\\\x0d\\\\x0a\\\\x1a\\\\x0a\\\\x00\\\\x00\\\\x00\\\\x0dIHDR");
  ok &= ExpectContains(base / "PlainText.txt",
                       "This subsystem schedules jobs and retries transient failures automatically.");

  if (fs::exists(base / "Unknown.txt")) {
    std::cerr << "debug_language_output_test failed: unexpected Unknown.txt\n";
    ok = false;
  }

  if (!ok)
    return 1;

  std::cout << "Debug language output test passed for " << fixturePath << ".\n";
  return 0;
}
