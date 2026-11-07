//
// RawRewrite.hpp
// Author: Antoine Bastide
//
// Counts the toothpicks (backslashes) left after rewriting a string literal as a
// raw literal R"(...)". A simple escape is removed when the raw literal can hold
// its character literally (\n, \t, \", ...); \\ leaves one backslash. A simple
// escape for a character a raw literal cannot hold (\r, \a, \b, \f, \v, ...)
// keeps its toothpick; carriage return is one because compilers normalize line
// endings in source (Go discards CR in raw strings). An escape that encodes a
// character by number or name (\x41, \u00e9, \101, \N{...}, ...) stays as
// written and is not counted, before or after the rewrite.
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
      /// Toothpicks in the raw literal.
      uint64_t Count();
      /// After Count(): true if no simple escape needs a character a raw literal cannot hold.
      [[nodiscard]] bool Holdable() const;
      /// The raw-literal content: decoded simple escapes, encoding escapes as written.
      [[nodiscard]] std::string_view Content() const { return bytes; }
      /// Backslashes of the encoding escapes, which are not toothpicks.
      [[nodiscard]] uint64_t EncodingBackslashes() const { return encodingBackslashes; }
    private:
      static constexpr int32_t kLiteral = -1;
      static constexpr int32_t kEncoding = -2;

      Dialect dialect;
      /// Raw-literal content; owner is the escape index, kLiteral, or kEncoding.
      std::string bytes;
      std::vector<int32_t> owner;
      /// Per escape: 0 removed, 1 non-holdable character, 2 decoded backslash.
      std::vector<uint8_t> survives;
      uint64_t encodingBackslashes = 0;

      void push(uint8_t byte, int32_t id);
      void encoding(std::string_view text);
      [[nodiscard]] bool isEncoding(std::string_view rest) const;
  };

  /// True if the language's default raw string form holds `content` verbatim.
  /// `quote` is the literal's opening quote run; Python and Dart keep its style.
  bool DefaultRawHolds(LanguageEnum language, std::string_view content, std::string_view quote);
  /// True if a matchertext literal M"(...)" holds `content` verbatim.
  bool MatchertextHolds(std::string_view content);
}

#endif // RAW_REWRITE_HPP
