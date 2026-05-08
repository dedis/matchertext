//
// LanguageParser.cpp
// Author: Antoine Bastide
// Date: 29.04.2026
//

#include <algorithm>
#include <array>
#include <initializer_list>
#include <unordered_map>

#include "../include/LanguageParser.hpp"
#include "JSON.hpp"
#include "JsonParser.hpp"
#include "../include/Parser.hpp"

struct LangSpec {
  std::initializer_list<const char *> compilers;
  const char *cmdTemplate;
};

#ifndef MATCHERTEXT_PARSERS_DIR
#define MATCHERTEXT_PARSERS_DIR "./parsers"
#endif
#ifndef MATCHERTEXT_GO_PARSER_BIN
#define MATCHERTEXT_GO_PARSER_BIN "./matchertext_go_parser"
#endif

// C and C++ are not included because they use the clang parser compiled into the parser.
// The Go parser is pre-compiled by CMake to avoid `go run` recompiling on every file
// and to sidestep its same-directory rule for .go arguments.
static const std::unordered_map<Language, LangSpec> specs = {
  {Language::Go, {{MATCHERTEXT_GO_PARSER_BIN}, "\"{}\" \"{}\""}},
  {Language::Python, {{"python3", "python"}, "{} \"" MATCHERTEXT_PARSERS_DIR "/parser.py\" \"{}\""}},
};

static std::string format(const std::string &tpl, const std::string &a, const std::string &b) {
  std::string out;
  out.reserve(tpl.size() + a.size() * 2 + b.size() * 2);

  int arg = 0;
  for (size_t i = 0; i < tpl.size(); ++i) {
    if (i + 1 < tpl.size() && tpl[i] == '{' && tpl[i + 1] == '}') {
      if (arg == 0)
        out += a;
      else if (arg == 1)
        out += b;
      else if (arg == 2 || arg == 3)
        out += b + ".out";
      ++arg;
      ++i;
    } else {
      out += tpl[i];
    }
  }
  return out;
}

static bool isAvailable(const char *cmd) {
  const std::string check = "command -v " + std::string(cmd) + " >/dev/null 2>&1";
  return std::system(check.c_str()) == 0;
}

static std::string firstAvailable(const std::initializer_list<const char *> candidates) {
  for (const auto *c: candidates)
    if (isAvailable(c))
      return c;
  return "";
}

bool LanguageParser::ExtractData(
  const Language language, const std::string &compilerOverride, const std::string &filePath, Serde::JSON &result
) {
  // Use the built-in clang parser
  if (language == Language::C || language == Language::CPP)
    return Parser::ParseC_CPP(filePath, result) && result.IsArray();

  std::string out;
  if (!RunBuildCommand(language, compilerOverride, filePath, out))
    return false;

  result = Serde::JSONParser{out}.Parse();
  return result.IsArray();
}

bool LanguageParser::ParseLanguage(const std::string &name, Language &out) {
  std::string lower(name);
  std::ranges::transform(
    lower, lower.begin(),
    [](const unsigned char c) {
      return std::tolower(c);
    }
  );
  if (lower == "c") {
    out = Language::C;
    return true;
  }
  if (lower == "cpp" || lower == "c++") {
    out = Language::CPP;
    return true;
  }
  if (lower == "go" || lower == "golang") {
    out = Language::Go;
    return true;
  }
  if (lower == "python" || lower == "py") {
    out = Language::Python;
    return true;
  }
  return false;
}

bool LanguageParser::RunBuildCommand(
  const Language language, const std::string &compilerOverride, const std::string &filePath, std::string &out
) {
  // Create the correct build command
  const auto it = specs.find(language);
  if (it == specs.end())
    return false;

  const std::string cc = compilerOverride.empty() ? firstAvailable(it->second.compilers) : compilerOverride;
  if (cc.empty())
    return false;

  std::array<char, 4096> buffer{};
  const std::string cmd = format(it->second.cmdTemplate, cc, filePath);
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe)
    throw std::runtime_error("popen() failed");

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
    out += buffer.data();

  int status = pclose(pipe);
  if (status == -1)
    throw std::runtime_error("pclose() failed");

  if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
    throw std::runtime_error("command failed with exit code " + std::to_string(WEXITSTATUS(status)));

  return true;
}
