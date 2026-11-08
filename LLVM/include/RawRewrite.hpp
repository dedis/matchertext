//
// RawRewrite.hpp
// Author: Antoine Bastide
//
// Counts the toothpicks (backslashes) left after rewriting a string literal as a
// raw literal R"(...)". The rewrite decodes every escape, including the ones that
// encode a character by number or name (\x41, \u00e9, \101, \N{...}, ...), and
// removes its backslash; \\ and any escape that decodes to a backslash leave one.
// Decoded characters that a raw literal cannot hold are normalized: \r\n and a
// lone \r become a line feed, \f and \v a space, and other control characters
// (\a, \b, \e, \x00, ...) are dropped. An escape whose bytes are not valid UTF-8,
// or a lone UTF-16 surrogate, cannot be written and keeps its toothpick.
//

#ifndef RAW_REWRITE_HPP
#define RAW_REWRITE_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "LanguageClassifier.hpp"

namespace RawRewrite {
  /// How one language decodes its escape sequences.
  struct Dialect {
    enum class Digits : uint8_t { Octal, Decimal, None };

    /// Escape letters that decode to control characters, a subset of "abdefnrtv"
    /// (\d is DEL in Erlang and Elixir). Other letters decode to themselves.
    std::string_view controls = "abefnrtv";
    /// Escape characters that decode to nothing (Lua \z, Haskell \&).
    std::string_view empty = "";
    /// Base of escapes that start with a digit, which are encodings; None decodes
    /// the digit to itself (Dart).
    Digits digits = Digits::Octal;
    /// An unrecognized escape keeps its backslash (PHP, OCaml).
    bool unknownKeepsBackslash = false;
    /// \c, \C-, and \M- encode control or meta characters (Ruby, Perl).
    bool controlLetters = false;
    /// \x and digit escapes up to 0xFF encode a byte, not a code point (C, Go, Ruby, ...).
    bool byteEscapes = false;
  };

  /// Collects the content of one literal in source order and counts the
  /// toothpicks that survive the raw rewrite. Reuse one instance with Reset().
  class Counter {
    public:
      void Reset(const Dialect &next);
      /// Source text that the raw literal keeps verbatim.
      void Literal(std::string_view text);
      /// One escape sequence, starting with a backslash.
      void Escape(std::string_view text);
      /// Toothpicks in the raw literal; also builds Content().
      uint64_t Count();
      /// After Count(): true if every escape decodes to characters a raw literal can hold.
      [[nodiscard]] bool Holdable() const;
      /// After Count(): the raw-literal content, every escape decoded and normalized.
      [[nodiscard]] std::string_view Content() const { return content; }
    private:
      static constexpr int32_t kLiteral = -1;

      Dialect dialect;
      /// Decoded bytes before normalization; owner is the escape index or kLiteral.
      std::string bytes;
      std::vector<int32_t> owner;
      /// Per escape: 0 removed, 1 cannot be written, 2 decoded backslash.
      std::vector<uint8_t> survives;
      std::string content;
      /// A \u high-surrogate escape waiting for its low half: its index, or -1.
      int32_t pendingHigh = -1;
      uint32_t pendingValue = 0;

      void push(uint8_t byte, int32_t id);
      void pushCodePoint(uint32_t cp, int32_t id);
      void flushPending();
      /// Decodes an escape that encodes a character by number or name. `high` is
      /// the escape a low surrogate pairs with; `paired` is set when it does.
      [[nodiscard]] bool decodeEncoding(std::string_view rest, int32_t id, int32_t high, bool &paired);
  };

  /// True if the language's default raw string form holds `content` verbatim.
  /// `quote` is the literal's opening quote run; Python and Dart keep its style.
  bool DefaultRawHolds(LanguageEnum language, std::string_view content, std::string_view quote);
  /// True if a matchertext literal M"(...)" holds `content` verbatim.
  bool MatchertextHolds(std::string_view content);
}

#endif // RAW_REWRITE_HPP
