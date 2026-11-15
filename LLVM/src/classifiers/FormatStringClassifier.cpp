#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsIdentifierChar(const char c) {
      return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

    /// Placeholders of brace and interpolation formats: {} {0} {0:X} {:?} {name}
    /// {name:>8} (Rust, Python, C#), ${name} (JavaScript, Kotlin, shell), and
    /// \(name) (Swift). A brace right after a word, ], ), or } is a regex
    /// quantifier, a LaTeX argument, or a code block instead, and one after . or *
    /// is a glob alternative (*.{js,ts}), so these are skipped.
    int CountBracePlaceholders(const std::string_view s) {
      /// Code with an empty block (pub fn foo() {}, class B extends A {}) is not a
      /// format string.
      const auto trimmed = Trim(s);
      for (const auto keyword: {"pub ", "fn ", "class ", "public ", "private ", "protected ", "static ",
                                "struct ", "impl ", "interface ", "trait ", "enum ", "function "})
        if (trimmed.starts_with(keyword))
          return 0;
      int count = 0;
      for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '(') {
          size_t j = i + 2;
          while (j < s.size() && (IsIdentifierChar(s[j]) || s[j] == '.' || s[j] == '!' || s[j] == '?'))
            j++;
          if (j > i + 2 && j < s.size() && s[j] == ')' && !std::isdigit(static_cast<unsigned char>(s[i + 2]))) {
            count++;
            i = j;
          }
          continue;
        }
        if (s[i] != '{')
          continue;
        /// {{ name }} and {{ a.b | filter }} (Mustache, Jinja, Go templates).
        if (i + 1 < s.size() && s[i + 1] == '{') {
          const size_t close = s.find("}}", i + 2);
          if (close == std::string_view::npos)
            continue;
          const auto inner = Trim(s.substr(i + 2, close - i - 2));
          if (!inner.empty() && inner.find_first_of("{};\n") == std::string_view::npos) {
            count++;
            i = close + 1;
          }
          continue;
        }
        const bool dollar = i > 0 && s[i - 1] == '$';
        if (!dollar && i > 0 && (IsIdentifierChar(s[i - 1]) || s[i - 1] == ']' || s[i - 1] == ')' ||
                                 s[i - 1] == '}' || s[i - 1] == '{' || s[i - 1] == '\\' ||
                                 s[i - 1] == '.' || s[i - 1] == '*'))
          continue;
        size_t j = i + 1;
        while (j < s.size() && (IsIdentifierChar(s[j]) || s[j] == '.' || s[j] == '-'))
          j++;
        const bool named = j > i + 1;
        /// {backend-id} is a placeholder; a GUID {57DC5D59-11C2-...} is not.
        if (const auto name = s.substr(i + 1, j - i - 1);
          name.find('-') != std::string_view::npos &&
          (name.size() == 36 || !std::isalpha(static_cast<unsigned char>(name.front()))))
          continue;
        if (!dollar && j < s.size() && (s[j] == ':' || s[j] == ',' || s[j] == '!')) {
          j++;
          while (j < s.size() && s[j] != '}' && s[j] != '{' && !std::isspace(static_cast<unsigned char>(s[j])))
            j++;
        }
        if (j >= s.size() || s[j] != '}' || (dollar && !named))
          continue;
        /// An empty {} after ) is a function body, not a placeholder.
        if (!named && j == i + 1) {
          const auto before = TrimRight(s.substr(0, i));
          if (!before.empty() && before.back() == ')')
            continue;
        }
        count++;
        i = j;
      }
      return count;
    }
  } // namespace

  ClassificationResult DetectFormatString(const std::string_view s) {
    auto isFlag = [](const char c) {
      return c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' || c == '\'';
    };
    auto isConversion = [](const char c) {
      switch (c) {
        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
        case 'c':
        case 's':
        case 'p':
        case 'n':
        case 'm':
        // Go verbs
        case 'v':
        case 'T':
        case 'q':
        case 'w':
        case 'U':
          return true;
        default:
          return false;
      }
    };

    int specs = 0;
    for (size_t i = 0; i + 1 < s.size(); i++) {
      if (s[i] != '%')
        continue;
      if (s[i + 1] == '%') {
        i++;
        continue;
      }

      size_t j = i + 1;

      const size_t positionalStart = j;
      while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
        j++;
      if (j < s.size() && s[j] == '$' && j > positionalStart) {
        j++;
      } else {
        j = i + 1;
      }

      while (j < s.size() && isFlag(s[j]))
        j++;

      if (j < s.size() && s[j] == '*') {
        j++;
      } else {
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
          j++;
      }

      if (j < s.size() && s[j] == '.') {
        j++;
        if (j < s.size() && s[j] == '*') {
          j++;
        } else {
          while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
            j++;
        }
      }

      if (j + 1 < s.size()) {
        if ((s[j] == 'h' && s[j + 1] == 'h') || (s[j] == 'l' && s[j + 1] == 'l')) {
          j += 2;
        } else if (s[j] == 'h' || s[j] == 'l' || s[j] == 'j' || s[j] == 'z' ||
                   s[j] == 't' || s[j] == 'L') {
          j++;
        }
      } else if (j < s.size() &&
                 (s[j] == 'h' || s[j] == 'l' || s[j] == 'j' || s[j] == 'z' ||
                  s[j] == 't' || s[j] == 'L')) {
        j++;
      }

      if (j < s.size() && isConversion(s[j])) {
        specs++;
        i = j;
      }
    }
    specs += CountBracePlaceholders(s);
    if (specs == 0)
      return {LanguageEnum::Unknown, 0.0f};
    const float density = static_cast<float>(specs) / static_cast<float>(s.size());
    return {LanguageEnum::FormatString, std::min(0.5f + density * 10.0f, 0.95f)};
  }
} // namespace classifier_internal
