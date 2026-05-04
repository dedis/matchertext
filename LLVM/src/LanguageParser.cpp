//
// LanguageParser.cpp
// Author: Antoine Bastide
// Date: 29.04.2026
//

#include <algorithm>
#include <array>

#include "../include/LanguageParser.hpp"
#include "../include/JSON.hpp"
#include "../include/JsonParser.hpp"
#include "../include/LanguageData.hpp"
#include "../include/Parser.hpp"

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

static bool isAvailable(const std::string_view cmd) {
  const std::string check = "command -v " + std::string(cmd) + " >/dev/null 2>&1";
  return std::system(check.c_str()) == 0;
}

static std::string firstAvailable(const std::span<const std::string_view> candidates) {
  for (const auto c: candidates)
    if (isAvailable(c))
      return std::string(c);
  return "";
}

bool LanguageParser::ExtractData(
  const LanguageEnum language, const std::string &compilerOverride, const std::string &filePath, JSON &result
) {
  // Use the built-in clang parser
  if (language == LanguageEnum::C || language == LanguageEnum::CPP)
    return Parser::ParseC_CPP(filePath, result) && result.IsArray();

  std::string out;
  if (!RunBuildCommand(language, compilerOverride, filePath, out))
    return false;

  result = JSONParser{out}.Parse();
  return result.IsArray();
}

bool LanguageParser::ParseLanguage(const std::string &name, LanguageEnum &out) {
  std::string lower(name);
  std::ranges::transform(
    lower, lower.begin(), [](const unsigned char c) {
      return std::tolower(c);
    }
  );
  out = GetLanguage(lower);
  return out != LanguageEnum::Unknown;
}

bool LanguageParser::RunBuildCommand(
  const LanguageEnum language, const std::string &compilerOverride, const std::string &filePath, std::string &out
) {
  // Create the correct build command
  const auto data = GetLanguageData(language);
  const std::string cc = compilerOverride.empty() ? firstAvailable(data.compilers) : compilerOverride;
  if (cc.empty())
    return false;

  std::array<char, 4096> buffer{};
  const std::string cmd = format(std::string(data.cmdTemplate), cc, filePath);
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
