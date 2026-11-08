//
// matcher_text_test.cpp
// Author: Antoine Bastide
// Date: 05.11.2026
//
// Unit tests for AnalyzeMatcherText. The expected unmatched counts in strict
// mode are the counts that the Go scanner (go/matchertext/unmatched.go,
// UnmatchedOffsets) returns for the same inputs. The valid depth of a matched
// pair counts only the matched pairs that enclose it, itself included.
//
// Also tests MatcherScanIsConsistent, the check behind the assertion in
// AnalyzeMatcherText, and compares AnalyzeMatcherText on random inputs with a
// separate port of the recursive Go scanner.
//

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

#include "MatcherText.hpp"

namespace {
int failures = 0;
int checks = 0;

void ExpectConsistent(const std::string_view label, const MatcherScanResult &r, const uint64_t matchedPairs,
                      const bool consistent) {
  ++checks;
  if (MatcherScanIsConsistent(r, matchedPairs) == consistent)
    return;
  ++failures;
  std::cerr << "matcher_text_test FAILED: " << label << ": MatcherScanIsConsistent should be "
      << (consistent ? "true" : "false") << '\n';
}

// Reference scanner: a direct port of the recursive Go unmatchedScan
// (go/matchertext/unmatched.go), extended with depths. It shares no code with
// AnalyzeMatcherText. Relaxed mode lets any closer end the innermost opener.
struct Reference {
  std::string_view text;
  bool relaxed;
  size_t pos = 0;
  uint64_t unmatched = 0;
  uint64_t maxDepth = 0;
  uint64_t matchedPairs = 0;

  // Scans until the end, or until an unmatched closer when !all, as in Go.
  // Returns the max valid depth of the matched pairs scanned.
  uint64_t Scan(const bool all, const uint64_t openDepth) {
    uint64_t best = 0;
    while (pos < text.size()) {
      const char b = text[pos];
      if (IsOpener(b)) {
        ++pos;
        maxDepth = std::max(maxDepth, openDepth + 1);
        const uint64_t inner = Scan(false, openDepth + 1);
        if (pos < text.size() && (relaxed || IsMatched(b, text[pos]))) {
          ++pos; // the matching closer
          ++matchedPairs;
          best = std::max(best, inner + 1);
        } else {
          ++unmatched; // unmatched opener; the closer, if any, stays for the outer level
          best = std::max(best, inner);
        }
      } else if (IsCloser(b)) {
        if (!all)
          return best;
        ++unmatched; // closer that matches no opener
        ++pos;
      } else {
        ++pos;
      }
    }
    return best;
  }
};

// Compares AnalyzeMatcherText with the reference on random strings of matchers,
// and checks that every result is consistent.
void ExpectMatchesReference(const int samples, const size_t maxLength) {
  std::mt19937 rng(20261105); // fixed seed: the same strings on every run
  std::uniform_int_distribution<size_t> lengthDist(0, maxLength);
  constexpr std::string_view alphabet = "()[]{}x";
  std::uniform_int_distribution<size_t> charDist(0, alphabet.size() - 1);
  std::string text;
  for (int i = 0; i < samples; ++i) {
    text.clear();
    const size_t length = lengthDist(rng);
    for (size_t j = 0; j < length; ++j)
      text.push_back(alphabet[charDist(rng)]);
    for (const bool relaxed: {false, true}) {
      ++checks;
      const MatcherScanResult r = AnalyzeMatcherText(text, relaxed);
      Reference ref{text, relaxed};
      const uint64_t validDepth = ref.Scan(true, 0);
      if (r.unmatched == ref.unmatched && r.maxDepth == ref.maxDepth && r.maxValidDepth == validDepth &&
          r.rawChars == text.size() && MatcherScanIsConsistent(r, ref.matchedPairs))
        continue;
      ++failures;
      std::cerr << "matcher_text_test FAILED: random input " << '"' << text << '"'
          << (relaxed ? " (relaxed)" : " (strict)")
          << "\n  reference unmatched=" << ref.unmatched << " maxDepth=" << ref.maxDepth
          << " maxValidDepth=" << validDepth << " matchedPairs=" << ref.matchedPairs
          << "\n  actual    unmatched=" << r.unmatched << " maxDepth=" << r.maxDepth
          << " maxValidDepth=" << r.maxValidDepth << '\n';
      if (failures >= 10)
        return; // enough to diagnose; do not flood the output
    }
  }
}

void Expect(const std::string_view label, const std::string_view text, const bool relaxed,
            const uint64_t unmatched, const uint64_t maxDepth, const uint64_t maxValidDepth) {
  ++checks;
  const MatcherScanResult r = AnalyzeMatcherText(text, relaxed);
  if (r.unmatched == unmatched && r.maxDepth == maxDepth && r.maxValidDepth == maxValidDepth &&
      r.rawChars == text.size())
    return;
  ++failures;
  std::cerr << "matcher_text_test FAILED: " << label << (relaxed ? " (relaxed)" : " (strict)")
      << "\n  expected unmatched=" << unmatched << " maxDepth=" << maxDepth
      << " maxValidDepth=" << maxValidDepth << " rawChars=" << text.size()
      << "\n  actual   unmatched=" << r.unmatched << " maxDepth=" << r.maxDepth
      << " maxValidDepth=" << r.maxValidDepth << " rawChars=" << r.rawChars << '\n';
}
} // namespace

int main() {
  // --- Strict mode; unmatched counts match the Go scanner ---
  Expect("empty", "", false, 0, 0, 0);
  Expect("no matchers", "abc", false, 0, 0, 0);
  Expect("balanced siblings", "([]{})", false, 0, 2, 2);
  Expect("balanced, three levels", "{[(x)]}((y))", false, 0, 3, 3);
  Expect("unclosed openers add no valid level", "((((((((((x)", false, 9, 10, 1);
  Expect("unclosed opener around a pair", "(()", false, 1, 2, 1);
  Expect("closer that matches no opener", ")(", false, 2, 1, 0);
  Expect("mismatched closer ends the opener", "(]x)", false, 3, 1, 0);
  Expect("mismatched closer ends all inner openers", "(((]()", false, 4, 3, 1);
  Expect("mismatched closer matches an outer opener", "[(])", false, 2, 2, 1);
  Expect("crossed pairs", "([)]", false, 2, 2, 1);

  // Regression for the WebAssembly bytes in vips/worker.min.js: many openers
  // that never close, then one mismatched closer, then one pair.
  const std::string manyOpeners = std::string(1000, '(') + "]()";
  Expect("1000 unclosed openers, mismatched closer, pair", manyOpeners, false, 1001, 1000, 1);

  // --- Relaxed mode: any closer closes the innermost opener ---
  Expect("any closer matches", "(]", true, 0, 1, 1);
  Expect("crossed pairs match", "[(])", true, 0, 2, 2);
  Expect("unclosed openers add no valid level", "((((((((((x)", true, 9, 10, 1);
  Expect("closer that matches no opener", ")(", true, 2, 1, 0);

  // --- Consistency check used by the assertion in AnalyzeMatcherText ---
  ExpectConsistent("empty result", MatcherScanResult{}, 0, true);
  ExpectConsistent("valid depth equal to matched pairs and raw depth", MatcherScanResult{0, 3, 3, 6}, 3, true);
  ExpectConsistent("valid depth below matched pairs", MatcherScanResult{0, 2, 2, 12}, 6, true);
  ExpectConsistent("valid depth above matched pairs", MatcherScanResult{0, 5, 4, 8}, 3, false);
  ExpectConsistent("valid depth above raw depth", MatcherScanResult{0, 2, 3, 8}, 4, false);
  // The old scanner's result for "((((((((((x)": valid depth 10 from 1 matched pair.
  ExpectConsistent("old scanner result for unclosed openers", MatcherScanResult{9, 10, 10, 12}, 1, false);

  // --- Random inputs against the Go-port reference ---
  ExpectMatchesReference(200000, 32);

  if (failures == 0) {
    std::cout << "matcher_text_test: all " << checks << " checks passed.\n";
    return 0;
  }
  std::cerr << "matcher_text_test: " << failures << " of " << checks << " checks failed.\n";
  return 1;
}
