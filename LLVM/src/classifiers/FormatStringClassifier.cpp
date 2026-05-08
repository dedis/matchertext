#include "Internal.hpp"

namespace classifier_internal {
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
    if (specs == 0)
      return {Language::Unknown, 0.0f};
    const float density = static_cast<float>(specs) / static_cast<float>(s.size());
    return {Language::FormatString, std::min(0.5f + density * 10.0f, 0.95f)};
  }
} // namespace classifier_internal
