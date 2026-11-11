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

bool CheckLanguageCount(const LanguageStatsSnapshot &snapshot, const LanguageEnum language,
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
  // Unclassified strings count as Unknown, so every string is in the total.
  ok &= CheckEq("basic.lang.total", static_cast<double>(lang.total), 6.0);
  ok &= CheckLanguageCount(lang, LanguageEnum::Unknown, 3);
  ok &= CheckLanguageCount(lang, LanguageEnum::IdentifierLike, 1);

  const uint64_t rawLevel1 =
      stringNested.rawLevels.size() > 1 ? stringNested.rawLevels[1] : 0;
  const uint64_t validLevel1 =
      stringNested.validLevels.size() > 1 ? stringNested.validLevels[1] : 0;
  ok &= CheckEq("basic.nesting.raw_level_1", static_cast<double>(rawLevel1), 4.0);
  ok &= CheckEq("basic.nesting.valid_level_1", static_cast<double>(validLevel1), 4.0);
  ok &= CheckLanguageCount(lang, LanguageEnum::PlainText, 2);
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

  // Converted-form toothpicks. kRegex is a raw literal whose body holds 4 literal
  // backslashes (\[ \] \( \)); converting an already-raw literal keeps all 4, so it
  // must not be re-decoded down to 0. kJson's "\"" escapes (6 toothpicks) collapse to
  // 0, and kSql has none — total converted = 4, all from the single raw kRegex sample.
  ok &= CheckEq("raw.strings.toothpicks_converted", strings.toothpicksConverted, 4.0);
  ok &= CheckEq("raw.strings.with_toothpicks_converted", strings.withToothpicksConverted, 1.0);
  ok &= CheckEq("raw.strings.toothpicks_converted_max", strings.toothpicksConvertedMax, 4.0);
  // Total reduction weights by toothpick count: (10-4)/10 = 60%. The per-sample mean
  // weights each sample equally: (0% for kSql, 100% for kJson, 0% for kRegex)/3 ≈ 33.3%.
  // The two metrics are now genuinely distinct.
  ok &= CheckEq("raw.strings.reduction_total", strings.toothpicksReductionTotal, 60.0);
  ok &= CheckEq("raw.strings.reduction_avg", strings.toothpicksReductionAvg, 100.0 / 3.0);
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
  ok &= CheckLanguageCount(lang, LanguageEnum::SQL, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::JSON, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::Regex, 1);
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
  // ([)] crosses: strict, neither the comments nor the file comply; relaxed, both do.
  ok &= CheckEq("comments.file.neither", file.neitherCompliant, 1.0);
  ok &= CheckEq("comments.file.both_relaxed", file.bothCompliantRelaxed, 1.0);
  ok &= CheckEq("comments.file.fragments_only_relaxed", file.fragmentsOnlyCompliantRelaxed, 0.0);
  ok &= CheckEq("comments.file.whole_only_relaxed", file.wholeOnlyCompliantRelaxed, 0.0);
  ok &= CheckEq("comments.file.neither_relaxed", file.neitherCompliantRelaxed, 0.0);
  ok &= CheckEq("comments.lang.total", static_cast<double>(lang.total), 1.0);
  ok &= CheckLanguageCount(lang, LanguageEnum::IdentifierLike, 1);

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
  ok &= CheckEq("prefixed.lang.total", static_cast<double>(lang.total), 5.0);
  ok &= CheckLanguageCount(lang, LanguageEnum::IdentifierLike, 1);

  const uint64_t rawLevel1 =
      stringNested.rawLevels.size() > 1 ? stringNested.rawLevels[1] : 0;
  const uint64_t validLevel1 =
      stringNested.validLevels.size() > 1 ? stringNested.validLevels[1] : 0;
  ok &= CheckEq("prefixed.nesting.raw_level_1", static_cast<double>(rawLevel1), 1.0);
  ok &= CheckEq("prefixed.nesting.valid_level_1", static_cast<double>(validLevel1), 1.0);
  ok &= CheckLanguageCount(lang, LanguageEnum::URL, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::SQL, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::HTML, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::JSON, 1);
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
  ok &= CheckEq("bucket_mix.strings.count", strings.count, 8.0);
  // kBinary and kAnsi are PseudoBinaryData, out of matchertext's scope: their 13
  // toothpicks count, but not toward the rewrite, and kAnsi's unmatched '[' bytes
  // are not violations and add no nesting.
  ok &= CheckEq("bucket_mix.strings.with_toothpicks", strings.withToothpicks, 2.0);
  ok &= CheckEq("bucket_mix.strings.toothpicks", strings.toothpicks, 13.0);
  ok &= CheckEq("bucket_mix.strings.scope_count", strings.scopeCount, 6.0);
  ok &= CheckEq("bucket_mix.strings.scope_toothpicks", strings.scopeToothpicks, 0.0);
  ok &= CheckEq("bucket_mix.strings.toothpicks_converted", strings.toothpicksConverted, 0.0);
  ok &= CheckEq("bucket_mix.strings.non_compliance", strings.nonComplianceCount, 0.0);
  ok &= CheckEq("bucket_mix.strings.compliance_rate", strings.complianceRate, 100.0);
  ok &= CheckEq("bucket_mix.strings.raw_chars", strings.rawChars, 334.0);
  ok &= CheckEq("bucket_mix.docs.count", docs.count, 0.0);
  ok &= CheckEq("bucket_mix.docs_relaxed.count", docsRelaxed.count, 0.0);
  ok &= CheckEq("bucket_mix.lang.total", static_cast<double>(lang.total), 8.0);
  ok &= CheckEq("bucket_mix.nesting.raw_levels_size",
                static_cast<double>(stringNested.rawLevels.size()), 0.0);
  ok &= CheckEq("bucket_mix.nesting.valid_levels_size",
                static_cast<double>(stringNested.validLevels.size()), 0.0);
  ok &= CheckLanguageCount(lang, LanguageEnum::PlainText, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::FilePath, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::FormatString, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::YAML, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::Shell, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::HexData, 1);
  ok &= CheckLanguageCount(lang, LanguageEnum::PseudoBinaryData, 2);
  ok &= CheckNoFileViolations();
  // kAnsi's unmatched '[' are blanked out of the whole file, like out of the fragments.
  const auto file = Parser::FILE_STATS.Snapshot();
  ok &= CheckEq("bucket_mix.file.whole_compliant", file.wholeCompliant, 1.0);
  ok &= CheckEq("bucket_mix.file.both", file.bothCompliant, 1.0);
  ok &= CheckEq("bucket_mix.file.fragment_free", file.fragmentFree, 0.0);
  return ok;
}

// The lexer reads one token past a string literal; a comment there must not be lost.
bool TestCommentAfterString() {
  const auto strings = Parser::STRING_STATS.Snapshot();
  const auto docs = Parser::DOCS_STATS.Snapshot();

  bool ok = true;
  ok &= CheckEq("comment_after_string.strings.count", strings.count, 3.0);
  ok &= CheckEq("comment_after_string.docs.count", docs.count, 4.0);
  ok &= CheckNoFileViolations();
  return ok;
}

/// The file's cell of fragments compliant x whole file compliant (strict), and
/// its single entry in the corpus-wide content registry.
bool CheckWholeFile(const std::string_view label, const bool fragments, const bool whole, const double strings) {
  const auto file = Parser::FILE_STATS.Snapshot();
  const std::string l(label);
  bool ok = true;
  ok &= CheckEq(l + ".file.whole_compliant", file.wholeCompliant, whole ? 1.0 : 0.0);
  ok &= CheckEq(l + ".file.both", file.bothCompliant, fragments && whole ? 1.0 : 0.0);
  ok &= CheckEq(l + ".file.fragments_only", file.fragmentsOnlyCompliant, fragments && !whole ? 1.0 : 0.0);
  ok &= CheckEq(l + ".file.whole_only", file.wholeOnlyCompliant, !fragments && whole ? 1.0 : 0.0);
  ok &= CheckEq(l + ".file.neither", file.neitherCompliant, !fragments && !whole ? 1.0 : 0.0);
  ok &= CheckEq(l + ".contents.distinct", static_cast<double>(Parser::FILE_CONTENTS.size()), 1.0);
  for (const auto &[hash, content]: Parser::FILE_CONTENTS) {
    ok &= CheckEq(l + ".contents.files", static_cast<double>(content.files), 1.0);
    ok &= CheckEq(l + ".contents.strings", static_cast<double>(content.strings), strings);
  }
  return ok;
}

/// One string literal per cause of the kept toothpicks: each cause keeps one, and
/// they add up to the converted toothpicks, per category and per content too.
bool TestKeptCauses() {
  const auto strings = Parser::STRING_STATS.Snapshot();
  const auto lang = Parser::STRING_LANG_STATS.Snapshot();
  bool ok = true;
  ok &= CheckEq("kept.strings.count", strings.count, 7.0);
  ok &= CheckEq("kept.strings.scope_count", strings.scopeCount, 7.0);
  ok &= CheckEq("kept.strings.toothpicks", strings.toothpicks, 8.0);
  ok &= CheckEq("kept.strings.converted", strings.toothpicksConverted, 6.0);
  ok &= CheckEq("kept.non_compliant", strings.keptNonCompliant, 1.0);
  ok &= CheckEq("kept.not_matchertext", strings.keptNotMatchertext, 1.0);
  ok &= CheckEq("kept.raw", strings.keptRaw, 1.0);
  ok &= CheckEq("kept.unknown_escape", strings.keptUnknownEscape, 0.0);
  ok &= CheckEq("kept.backslash", strings.keptBackslash, 1.0);
  ok &= CheckEq("kept.unwritable", strings.keptUnwritable, 1.0);
  ok &= CheckEq("kept.control", strings.keptControl, 1.0);
  double categoryToothpicks = 0, categoryConverted = 0;
  for (const auto &e: lang.entries) {
    categoryToothpicks += static_cast<double>(e.toothpicks);
    categoryConverted += static_cast<double>(e.converted);
  }
  ok &= CheckEq("kept.categories.toothpicks", categoryToothpicks, 8.0);
  ok &= CheckEq("kept.categories.converted", categoryConverted, 6.0);
  ok &= CheckEq("kept.contents.distinct", static_cast<double>(Parser::FILE_CONTENTS.size()), 1.0);
  for (const auto &[hash, content]: Parser::FILE_CONTENTS) {
    const auto &a = content.first;
    ok &= CheckEq("kept.contents.strings", static_cast<double>(a.strings), 7.0);
    ok &= CheckEq("kept.contents.compliant", static_cast<double>(a.compliantStrings), 6.0);
    ok &= CheckEq("kept.contents.toothpicks", static_cast<double>(a.scopeToothpicks), 8.0);
    ok &= CheckEq("kept.contents.converted", static_cast<double>(a.converted), 6.0);
    double kept = 0;
    for (const uint64_t k: a.kept)
      kept += static_cast<double>(k);
    ok &= CheckEq("kept.contents.kept", kept, 6.0);
    ok &= CheckEq("kept.contents.comments", static_cast<double>(a.docs), 1.0);
    ok &= CheckEq("kept.contents.fragments", a.fragments ? 1.0 : 0.0, 0.0);
    double categories = 0;
    for (const auto &c: a.categories)
      categories += static_cast<double>(c.count);
    ok &= CheckEq("kept.contents.categories", categories, 7.0);
  }
  return ok;
}

/// A file with no string literal and no comment.
bool TestFragmentFree() {
  const auto file = Parser::FILE_STATS.Snapshot();
  bool ok = true;
  ok &= CheckEq("fragment_free.file.count", file.count, 1.0);
  ok &= CheckEq("fragment_free.file.fragment_free", file.fragmentFree, 1.0);
  ok &= CheckEq("fragment_free.file.fragment_free_whole", file.fragmentFreeWholeCompliant, 1.0);
  ok &= CheckEq("fragment_free.file.fragment_free_whole_relaxed", file.fragmentFreeWholeCompliantRelaxed, 1.0);
  ok &= CheckEq("fragment_free.file.both", file.bothCompliant, 1.0);
  ok &= CheckEq("fragment_free.file.both_relaxed", file.bothCompliantRelaxed, 1.0);
  return ok;
}

bool RunFixture(const std::string &fixturePath) {
  const fs::path path(fixturePath);
  const std::string name = path.filename().string();

  if (Serde::JSON result; Parser::ParseC_CPP(path.string(), LanguageEnum::CPP, result))
    Parser::GatherStatistics(std::move(result), path.string(), path.string());

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
  if (name == "comment_after_string.cpp")
    return TestCommentAfterString();
  if (name == "whole_file_fragments_only.cpp")
    return CheckWholeFile("whole_file_fragments_only", true, false, 1.0);
  if (name == "whole_file_whole_only.cpp")
    return CheckWholeFile("whole_file_whole_only", false, true, 2.0);
  if (name == "kept_causes.cpp")
    return TestKeptCauses();
  if (name == "fragment_free.cpp")
    return TestFragmentFree();

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
