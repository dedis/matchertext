//
// MatcherText.hpp
// Author: Antoine Bastide
// Date: 09.03.2026
//

#ifndef MATCHER_TEXT_HPP
#define MATCHER_TEXT_HPP
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

[[nodiscard]] inline bool IsOpener(const char c) {
  return c == '(' || c == '[' || c == '{';
}

[[nodiscard]] inline bool IsCloser(const char c) {
  return c == ')' || c == ']' || c == '}';
}

[[nodiscard]] inline bool IsMatched(const char o, const char c) {
  return (o == '(' && c == ')') ||
         (o == '[' && c == ']') ||
         (o == '{' && c == '}');
}

struct MatcherScanResult {
  uint64_t unmatched = 0; // total unmatched matchers in this sample
  uint64_t maxDepth = 0; // max number of openers open at the same time, matched or not
  uint64_t maxValidDepth = 0; // max number of matched pairs enclosing a matched pair, itself included
  uint64_t rawChars = 0; // raw source characters in the extracted text
};

// Invariants of a scan result: a valid depth of d needs d matched pairs, and
// those d pairs are open at the same time, so the raw depth is at least d.
[[nodiscard]] inline bool MatcherScanIsConsistent(const MatcherScanResult &r, const uint64_t matchedPairs) {
  return r.maxValidDepth <= matchedPairs && r.maxValidDepth <= r.maxDepth;
}

// Aggregate-only equivalent of the Go unmatched scanner
// (go/matchertext/unmatched.go). A closer that does not match the innermost
// open opener makes that opener unmatched, then the next one out, until an
// opener matches the closer or none is left, in which case the closer is
// unmatched. In relaxed mode any closer matches. Openers still open at the end
// are unmatched. Unmatched openers add no level to the valid depth.
static MatcherScanResult AnalyzeMatcherText(const std::string_view text, const bool relaxed) {
  struct Open {
    char opener;
    uint64_t innerValidDepth; // max valid depth of the matched pairs inside this opener
  };
  MatcherScanResult r;
  uint64_t matchedPairs = 0;
  std::vector<Open> stack;

  // Close the innermost open opener and pass the valid depth below it to its
  // parent: one level more if the opener is matched, the same if it is not.
  auto close = [&](const bool matched) {
    const uint64_t depth = stack.back().innerValidDepth + (matched ? 1 : 0);
    stack.pop_back();
    uint64_t &outer = stack.empty() ? r.maxValidDepth : stack.back().innerValidDepth;
    outer = std::max(outer, depth);
  };

  for (const char c: text) {
    ++r.rawChars;

    if (IsOpener(c)) {
      stack.push_back({c, 0});
      r.maxDepth = std::max<uint64_t>(r.maxDepth, stack.size());
    } else if (IsCloser(c)) {
      // Each iteration pops one opener, so this ends after at most stack.size() steps.
      while (!relaxed && !stack.empty() && !IsMatched(stack.back().opener, c)) {
        ++r.unmatched;
        close(false);
      }
      if (stack.empty()) {
        ++r.unmatched;
      } else {
        ++matchedPairs;
        close(true);
      }
    }
  }

  while (!stack.empty()) {
    ++r.unmatched;
    close(false);
  }

  // A violation is a bug in this scanner, never bad input: stop the run rather
  // than write corrupt statistics. Checked in every build type (NDEBUG is set).
  if (!MatcherScanIsConsistent(r, matchedPairs)) {
    std::fprintf(stderr,
                 "AnalyzeMatcherText: inconsistent result: valid depth %llu, raw depth %llu, matched pairs %llu\n",
                 static_cast<unsigned long long>(r.maxValidDepth), static_cast<unsigned long long>(r.maxDepth),
                 static_cast<unsigned long long>(matchedPairs));
    std::abort();
  }
  return r;
}

#endif //MATCHER_TEXT_HPP
