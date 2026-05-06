#ifndef LANGUAGE_DATA_HPP
#define LANGUAGE_DATA_HPP

#ifndef MATCHERTEXT_PARSERS_DIR
#define MATCHERTEXT_PARSERS_DIR "./parsers"
#endif

#ifndef MATCHERTEXT_GO_PARSER_BIN
#define MATCHERTEXT_GO_PARSER_BIN "./matchertext_go_parser"
#endif

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
  /// The language's common compilers
  const std::span<const std::string_view> compilers;
  /// The language's compiler command call template
  const std::string_view cmdTemplate;
};

#define LANGUAGE_LIST(X)                                                     \
  X(C, ("c"), ("c", "h"), (), "")                                            \
  X(CPP, ("cpp", "c++"), ("cc", "cpp", "cxx", "hpp", "hh", "hxx"), (), "")   \
  X(Go, ("go"), ("go"), (MATCHERTEXT_GO_PARSER_BIN), R"("{}" "{}")")         \
  X(Python, ("python", "py"), ("py", "pyw", "pyi", "pyz", "pyzw"), ("python3", "python"), "{} \"" MATCHERTEXT_PARSERS_DIR "/parser.py\" \"{}\"")

// The code bellow is to make sure the data above ^^^^^ is compiled into the parser instead of being constructed at
// each parser startup for some speed optimizations.

template<typename... Ts> constexpr std::array<std::string_view, sizeof...(Ts)> sv_array(Ts... values) {
  return {std::string_view{values}...};
}

#define STRIP_PARENS(...) __VA_ARGS__
#define AS_ARRAY(x) sv_array(STRIP_PARENS x)

#define MAKE_ARRAYS(name, aliases, extensions, comps, cmd)                      \
  constexpr auto k##name##Aliases = AS_ARRAY(aliases);                          \
  constexpr auto k##name##Extensions = AS_ARRAY(extensions);                    \
  constexpr auto k##name##Compilers  = AS_ARRAY(comps);

LANGUAGE_LIST(MAKE_ARRAYS)

#undef MAKE_ARRAYS

#define MAKE_ENTRY(name, aliases, extensions, comps, cmd)                       \
  std::pair{                                                                    \
    LanguageEnum::name,                                                         \
    LanguageData{                                                               \
      std::span<const std::string_view>{k##name##Aliases},                      \
      std::span<const std::string_view>{k##name##Extensions},                   \
      std::span<const std::string_view>{k##name##Compilers},                    \
      std::string_view{cmd}                                                     \
    }                                                                           \
  },

constexpr auto kLanguageData = std::array{LANGUAGE_LIST(MAKE_ENTRY)};

#undef MAKE_ENTRY

static constexpr const LanguageData &GetLanguageData(const LanguageEnum lang) {
  for (const auto &[key, value]: kLanguageData)
    if (key == lang)
      return value;
  throw std::logic_error{"Unknown Language, make sure that the language is registered in LanguageData.hpp"};
}

static LanguageEnum GetLanguage(const std::string_view lang) {
  for (const auto &[key, value]: kLanguageData)
    if (const auto it = std::ranges::find(value.alias, lang); it != value.alias.end())
      return key;
  return LanguageEnum::Unknown;
}

#endif // LANGUAGE_DATA_HPP
