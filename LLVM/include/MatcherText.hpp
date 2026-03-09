//
// MatcherText.hpp
// Author: Antoine Bastide
// Date: 09.03.2026
//

#ifndef MATCHER_TEXT_HPP
#define MATCHER_TEXT_HPP
#include <cstdint>
#include <string_view>

static inline bool IsOpener(char c) {
  return c == '(' || c == '[' || c == '{';
}

static inline bool IsCloser(char c) {
  return c == ')' || c == ']' || c == '}';
}

static inline bool IsMatched(char o, char c) {
  return (o == '(' && c == ')') ||
         (o == '[' && c == ']') ||
         (o == '{' && c == '}');
}

struct MatcherScanResult {
  uint64_t unmatched = 0; // total unmatched matchers in this sample
  uint64_t maxDepth = 0; // max balanced nesting depth reached
  uint64_t rawChars = 0; // raw source characters in the extracted text
};

// Aggregate-only equivalent of the Go unmatched scanner.
// Counts unmatched closers immediately; unmatched openers remain on the stack
// and are added at end-of-sample.
static MatcherScanResult AnalyzeMatcherText(std::string_view text) {
  MatcherScanResult r;
  std::vector<char> stack;
  stack.reserve(text.size());

  for (const unsigned char uc: text) {
    char c = static_cast<char>(uc);
    ++r.rawChars;

    if (IsOpener(c)) {
      stack.push_back(c);
      r.maxDepth = std::max<uint64_t>(r.maxDepth, stack.size());
      continue;
    }

    if (IsCloser(c)) {
      if (stack.empty() || !IsMatched(stack.back(), c)) {
        ++r.unmatched; // unmatched or mismatched closer
      } else {
        stack.pop_back();
      }
    }
  }

  r.unmatched += stack.size(); // unmatched openers
  return r;
}

#endif //MATCHER_TEXT_HPP
