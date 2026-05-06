#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "JSON.hpp"
#include "Parser.hpp"

namespace fs = std::filesystem;

namespace {
bool CheckEq(const std::string_view label, const double actual, const double expected,
             const double epsilon = 1e-9) {
  if (std::fabs(actual - expected) <= epsilon)
    return true;

  std::cerr << "parser_fixture_test failed: " << label
            << " expected=" << expected << " actual=" << actual << '\n';
  return false;
}

bool CheckLanguageCount(const LanguageStatsSnapshot &snapshot, const Language language,
                        const uint64_t expectedCount) {
  for (const auto &entry : snapshot.entries) {
    if (entry.language != language)
      continue;
    if (entry.count == expectedCount)
      return true;

    std::cerr << "parser_fixture_test failed: language "
              << LanguageName(language) << " expected count=" << expectedCount
              << " actual=" << entry.count << '\n';
    return false;
  }

  std::cerr << "parser_fixture_test failed: missing language "
            << LanguageName(language) << " expected count=" << expectedCount << '\n';
  return false;
}

bool CheckNoFileViolations() {
  const auto file = Parser::FILE_STATS.Snapshot();
  return CheckEq("file.sample_size", file.count, 1.0) &&
         CheckEq("file.with_violation", file.withViolation, 0.0) &&
         CheckEq("file.violation_count", file.violationCount, 0.0) &&
         CheckEq("file.with_violation_relaxed", file.withViolationRelaxed, 0.0) &&
         CheckEq("file.violation_count_relaxed", file.violationCountRelaxed, 0.0);
}

bool TestBasicLiterals() {
  const auto strings = Parser::STRING_STATS.Snapshot();
  const auto docs = Parser::DOCS_STATS.Snapshot();
  const auto docsRelaxed = Parser::DOCS_RELAXED_STATS.Snapshot();
  const auto stringNested = Parser::STRING_NESTED_STATS.Snapshot();
  const auto lang = Parser::STRING_LANG_STATS.Snapshot();

  bool ok = true;
  ok &= CheckEq("basic.strings.count", strings.count, 6.0);
  ok &= CheckEq("basic.strings.with_toothpicks", strings.withToothpicks, 1.0);
  ok &= CheckEq("basic.strings.toothpicks", strings.toothpicks, 8.0);
  ok &= CheckEq("basic.strings.raw_chars", strings.rawChars, 73.0);
  ok &= CheckEq("basic.docs.count", docs.count, 1.0);
  ok &= CheckEq("basic.docs_relaxed.count", docsRelaxed.count, 1.0);
  ok &= CheckEq("basic.lang.total", static_cast<double>(lang.total), 2.0);

  const uint64_t rawLevel1 =
      stringNested.rawLevels.size() > 1 ? stringNested.rawLevels[1] : 0;
  const uint64_t validLevel1 =
      stringNested.validLevels.size() > 1 ? stringNested.validLevels[1] : 0;
  ok &= CheckEq("basic.nesting.raw_level_1", static_cast<double>(rawLevel1), 4.0);
  ok &= CheckEq("basic.nesting.valid_level_1", static_cast<double>(validLevel1), 4.0);
  ok &= CheckLanguageCount(lang, Language::PlainText, 2);
  ok &= CheckNoFileViolations();
  return ok;
}

bool TestRawAndAdjacent() {
  const auto strings = Parser::STRING_STATS.Snapshot();
  const auto docs = Parser::DOCS_STATS.Snapshot();
  const auto stringNested = Parser::STRING_NESTED_STATS.Snapshot();
  const auto lang = Parser::STRING_LANG_STATS.Snapshot();

  bool ok = true;
  ok &= CheckEq("raw.strings.count", strings.count, 3.0);
  ok &= CheckEq("raw.strings.with_toothpicks", strings.withToothpicks, 2.0);
  ok &= CheckEq("raw.strings.toothpicks", strings.toothpicks, 10.0);
  ok &= CheckEq("raw.strings.raw_chars", strings.rawChars, 94.0);
  ok &= CheckEq("raw.docs.count", docs.count, 1.0);
  ok &= CheckEq("raw.lang.total", static_cast<double>(lang.total), 3.0);

  const uint64_t rawLevel2 =
      stringNested.rawLevels.size() > 2 ? stringNested.rawLevels[2] : 0;
  const uint64_t rawLevel3 =
      stringNested.rawLevels.size() > 3 ? stringNested.rawLevels[3] : 0;
  const uint64_t validLevel2 =
      stringNested.validLevels.size() > 2 ? stringNested.validLevels[2] : 0;
  const uint64_t validLevel3 =
      stringNested.validLevels.size() > 3 ? stringNested.validLevels[3] : 0;
  ok &= CheckEq("raw.nesting.raw_level_2", static_cast<double>(rawLevel2), 2.0);
  ok &= CheckEq("raw.nesting.raw_level_3", static_cast<double>(rawLevel3), 1.0);
  ok &= CheckEq("raw.nesting.valid_level_2", static_cast<double>(validLevel2), 2.0);
  ok &= CheckEq("raw.nesting.valid_level_3", static_cast<double>(validLevel3), 1.0);
  ok &= CheckLanguageCount(lang, Language::SQL, 1);
  ok &= CheckLanguageCount(lang, Language::JSON, 1);
  ok &= CheckLanguageCount(lang, Language::Regex, 1);
  ok &= CheckNoFileViolations();
  return ok;
}

bool TestCommentsAndRelaxed() {
  const auto strings = Parser::STRING_STATS.Snapshot();
  const auto docs = Parser::DOCS_STATS.Snapshot();
  const auto docsRelaxed = Parser::DOCS_RELAXED_STATS.Snapshot();
  const auto docsNested = Parser::DOCS_NESTED_STATS.Snapshot();
  const auto docsRelaxedNested = Parser::DOCS_RELAXED_NESTED_STATS.Snapshot();
  const auto file = Parser::FILE_STATS.Snapshot();
  const auto lang = Parser::STRING_LANG_STATS.Snapshot();

  bool ok = true;
  ok &= CheckEq("comments.strings.count", strings.count, 1.0);
  ok &= CheckEq("comments.docs.count", docs.count, 4.0);
  ok &= CheckEq("comments.docs.non_compliance_with", docs.withNonCompliance, 2.0);
  ok &= CheckEq("comments.docs.non_compliance_total", docs.nonComplianceCount, 4.0);
  ok &= CheckEq("comments.docs_relaxed.count", docsRelaxed.count, 4.0);
  ok &= CheckEq("comments.docs_relaxed.non_compliance_with",
                docsRelaxed.withNonCompliance, 0.0);
  ok &= CheckEq("comments.docs_relaxed.non_compliance_total",
                docsRelaxed.nonComplianceCount, 0.0);
  ok &= CheckEq("comments.file.sample_size", file.count, 1.0);
  ok &= CheckEq("comments.file.with_violation", file.withViolation, 1.0);
  ok &= CheckEq("comments.file.violation_count", file.violationCount, 4.0);
  ok &= CheckEq("comments.file.with_violation_relaxed", file.withViolationRelaxed, 0.0);
  ok &= CheckEq("comments.file.violation_count_relaxed", file.violationCountRelaxed, 0.0);
  ok &= CheckEq("comments.lang.total", static_cast<double>(lang.total), 0.0);

  const uint64_t docsRawLevel2 =
      docsNested.rawLevels.size() > 2 ? docsNested.rawLevels[2] : 0;
  const uint64_t docsRawLevel3 =
      docsNested.rawLevels.size() > 3 ? docsNested.rawLevels[3] : 0;
  const uint64_t docsRelaxedRawLevel2 =
      docsRelaxedNested.rawLevels.size() > 2 ? docsRelaxedNested.rawLevels[2] : 0;
  const uint64_t docsRelaxedRawLevel3 =
      docsRelaxedNested.rawLevels.size() > 3 ? docsRelaxedNested.rawLevels[3] : 0;
  ok &= CheckEq("comments.docs.nesting.raw_level_2",
                static_cast<double>(docsRawLevel2), 1.0);
  ok &= CheckEq("comments.docs.nesting.raw_level_3",
                static_cast<double>(docsRawLevel3), 2.0);
  ok &= CheckEq("comments.docs_relaxed.nesting.raw_level_2",
                static_cast<double>(docsRelaxedRawLevel2), 1.0);
  ok &= CheckEq("comments.docs_relaxed.nesting.raw_level_3",
                static_cast<double>(docsRelaxedRawLevel3), 2.0);
  return ok;
}

bool TestPrefixedLiterals() {
  const auto strings = Parser::STRING_STATS.Snapshot();
  const auto docs = Parser::DOCS_STATS.Snapshot();
  const auto docsRelaxed = Parser::DOCS_RELAXED_STATS.Snapshot();
  const auto stringNested = Parser::STRING_NESTED_STATS.Snapshot();
  const auto lang = Parser::STRING_LANG_STATS.Snapshot();

  bool ok = true;
  ok &= CheckEq("prefixed.strings.count", strings.count, 5.0);
  ok &= CheckEq("prefixed.strings.with_toothpicks", strings.withToothpicks, 2.0);
  ok &= CheckEq("prefixed.strings.toothpicks", strings.toothpicks, 8.0);
  ok &= CheckEq("prefixed.strings.raw_chars", strings.rawChars, 175.0);
  ok &= CheckEq("prefixed.docs.count", docs.count, 0.0);
  ok &= CheckEq("prefixed.docs_relaxed.count", docsRelaxed.count, 0.0);
  ok &= CheckEq("prefixed.lang.total", static_cast<double>(lang.total), 4.0);

  const uint64_t rawLevel1 =
      stringNested.rawLevels.size() > 1 ? stringNested.rawLevels[1] : 0;
  const uint64_t validLevel1 =
      stringNested.validLevels.size() > 1 ? stringNested.validLevels[1] : 0;
  ok &= CheckEq("prefixed.nesting.raw_level_1", static_cast<double>(rawLevel1), 1.0);
  ok &= CheckEq("prefixed.nesting.valid_level_1", static_cast<double>(validLevel1), 1.0);
  ok &= CheckLanguageCount(lang, Language::URL, 1);
  ok &= CheckLanguageCount(lang, Language::SQL, 1);
  ok &= CheckLanguageCount(lang, Language::HTML, 1);
  ok &= CheckLanguageCount(lang, Language::JSON, 1);
  ok &= CheckNoFileViolations();
  return ok;
}

bool TestStringBucketMix() {
  const auto strings = Parser::STRING_STATS.Snapshot();
  const auto docs = Parser::DOCS_STATS.Snapshot();
  const auto docsRelaxed = Parser::DOCS_RELAXED_STATS.Snapshot();
  const auto stringNested = Parser::STRING_NESTED_STATS.Snapshot();
  const auto lang = Parser::STRING_LANG_STATS.Snapshot();

  bool ok = true;
  ok &= CheckEq("bucket_mix.strings.count", strings.count, 7.0);
  ok &= CheckEq("bucket_mix.strings.with_toothpicks", strings.withToothpicks, 1.0);
  ok &= CheckEq("bucket_mix.strings.toothpicks", strings.toothpicks, 9.0);
  ok &= CheckEq("bucket_mix.strings.raw_chars", strings.rawChars, 297.0);
  ok &= CheckEq("bucket_mix.docs.count", docs.count, 0.0);
  ok &= CheckEq("bucket_mix.docs_relaxed.count", docsRelaxed.count, 0.0);
  ok &= CheckEq("bucket_mix.lang.total", static_cast<double>(lang.total), 7.0);
  ok &= CheckEq("bucket_mix.nesting.raw_levels_size",
                static_cast<double>(stringNested.rawLevels.size()), 0.0);
  ok &= CheckEq("bucket_mix.nesting.valid_levels_size",
                static_cast<double>(stringNested.validLevels.size()), 0.0);
  ok &= CheckLanguageCount(lang, Language::PlainText, 1);
  ok &= CheckLanguageCount(lang, Language::FilePath, 1);
  ok &= CheckLanguageCount(lang, Language::FormatString, 1);
  ok &= CheckLanguageCount(lang, Language::YAML, 1);
  ok &= CheckLanguageCount(lang, Language::Shell, 1);
  ok &= CheckLanguageCount(lang, Language::HexData, 1);
  ok &= CheckLanguageCount(lang, Language::PseudoBinaryData, 1);
  ok &= CheckNoFileViolations();
  return ok;
}

bool RunFixture(const std::string &fixturePath) {
  const fs::path path(fixturePath);
  const std::string name = path.filename().string();

  if (Serde::JSON result; Parser::ParseC_CPP(path.string(), result))
    Parser::GatherStatistics(std::move(result), path.string());

  if (name == "basic_literals.cpp")
    return TestBasicLiterals();
  if (name == "raw_and_adjacent.cpp")
    return TestRawAndAdjacent();
  if (name == "comments_and_relaxed.cpp")
    return TestCommentsAndRelaxed();
  if (name == "prefixed_literals.cpp")
    return TestPrefixedLiterals();
  if (name == "string_bucket_mix.cpp")
    return TestStringBucketMix();

  std::cerr << "parser_fixture_test failed: unknown fixture " << fixturePath << '\n';
  return false;
}
} // namespace

int main(const int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: parser_fixture_test <fixture.cpp>\n";
    return 2;
  }

  if (!RunFixture(argv[1]))
    return 1;

  std::cout << "Parser fixture test passed for " << argv[1] << ".\n";
  return 0;
}
