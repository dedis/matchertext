#ifndef LANGUAGE_DATA_HPP
#define LANGUAGE_DATA_HPP

#include <array>
#include <span>
#include <string_view>
#include <utility>

#include "LanguageClassifier.hpp"

/// All the data associated with each language
struct LanguageData {
  /// The language's common aliases
  const std::span<const std::string_view> alias;
  /// The language's common file extensions
  const std::span<const std::string_view> extensions;
};

// Every language is parsed in-process (clang for C/C++, tree-sitter otherwise),
// so only aliases and extensions are needed. Order matters: extension lookup is
// first-wins by this list's order (main.cpp's try_emplace), so later entries
// never re-list an extension owned by an earlier one (e.g. Objective-C omits
// .h, which stays C).
#define LANGUAGE_LIST(X)                                                          \
  X(C, ("c"), ("c", "h"))                                                         \
  X(CPP, ("cpp", "c++"), ("cc", "cpp", "cxx", "hpp", "hh", "hxx"))                \
  X(Go, ("go"), ("go"))                                                           \
  X(Python, ("python", "py"), ("py", "pyw", "pyi", "pyz", "pyzw"))                \
  X(JavaScript, ("javascript", "js"), ("js", "mjs", "cjs", "jsx"))                \
  X(TypeScript, ("typescript", "ts"), ("ts", "mts", "cts", "tsx"))                \
  X(Java, ("java"), ("java"))                                                     \
  X(CSharp, ("csharp", "c#", "cs"), ("cs", "csx"))                                \
  X(Rust, ("rust", "rs"), ("rs"))                                                 \
  X(Ruby, ("ruby", "rb"), ("rb", "rbw", "rake", "gemspec"))                       \
  X(PHP, ("php"), ("php", "php3", "php4", "php5", "phtml"))                        \
  X(Perl, ("perl", "pl"), ("pl", "pm", "perl"))                                   \
  X(Lua, ("lua"), ("lua"))                                                        \
  X(Swift, ("swift"), ("swift"))                                                  \
  X(Kotlin, ("kotlin", "kt"), ("kt", "kts"))                                      \
  X(R, ("r"), ("r", "R"))                                                         \
  X(Scala, ("scala"), ("scala", "sc"))                                            \
  X(Haskell, ("haskell", "hs"), ("hs", "lhs"))                                    \
  X(OCaml, ("ocaml", "ml"), ("ml", "mli"))                                        \
  X(Erlang, ("erlang", "erl"), ("erl", "hrl"))                                    \
  X(Elixir, ("elixir", "ex"), ("ex", "exs"))                                      \
  X(Dart, ("dart"), ("dart"))                                                     \
  X(Objective_C, ("objc", "objectivec", "objective-c"), ("m", "mm"))              \
  X(GLSL, ("glsl"), ("glsl", "vert", "frag", "geom", "comp", "tesc", "tese"))     \
  X(HLSL, ("hlsl"), ("hlsl", "fx", "hlsli"))

// The code below ensures the data above is compiled into the parser instead of
// being constructed at each parser startup, for some speed optimizations.

template<typename... Ts> constexpr std::array<std::string_view, sizeof...(Ts)> sv_array(Ts... values) {
  return {std::string_view{values}...};
}

#define STRIP_PARENS(...) __VA_ARGS__
#define AS_ARRAY(x) sv_array(STRIP_PARENS x)

#define MAKE_ARRAYS(name, aliases, extensions)                                  \
  constexpr auto k##name##Aliases = AS_ARRAY(aliases);                          \
  constexpr auto k##name##Extensions = AS_ARRAY(extensions);

LANGUAGE_LIST(MAKE_ARRAYS)

#undef MAKE_ARRAYS

#define MAKE_ENTRY(name, aliases, extensions)                                   \
  std::pair{                                                                    \
    LanguageEnum::name,                                                         \
    LanguageData{                                                               \
      std::span<const std::string_view>{k##name##Aliases},                      \
      std::span<const std::string_view>{k##name##Extensions}                    \
    }                                                                           \
  },

constexpr auto kLanguageData = std::array{LANGUAGE_LIST(MAKE_ENTRY)};

#undef MAKE_ENTRY

static LanguageEnum GetLanguage(const std::string_view lang) {
  for (const auto &[key, value]: kLanguageData)
    if (const auto it = std::ranges::find(value.alias, lang); it != value.alias.end())
      return key;
  return LanguageEnum::Unknown;
}

#endif // LANGUAGE_DATA_HPP
