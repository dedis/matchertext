#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsHex(const char c) {
      return std::isxdigit(static_cast<unsigned char>(c)) != 0;
    }

    bool IsDigit(const char c) {
      return std::isdigit(static_cast<unsigned char>(c)) != 0;
    }

    void AppendUtf8(std::string &out, uint32_t cp) {
      if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = 0xFFFD;
      if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
      } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
    }

    uint32_t ParseHex(const std::string_view s) {
      uint32_t value = 0;
      for (const char c: s)
        value = value * 16 + static_cast<uint32_t>(IsDigit(c) ? c - '0' : (ToLower(c) - 'a' + 10));
      return value;
    }

    /// One token of a number: 42, -1, v1.2.3, 50px, 1.25rem, 2.4.4-2ubuntu1,
    /// 10.0.2.0/24, 127.0.0.1:5672, 2022-05-16T08:16:53Z, 123,456;789.0. It
    /// starts with a digit (after a sign, v, or dot), and its letters are units
    /// or suffixes: at most two per digit.
    bool IsNumberToken(std::string_view t) {
      if (!t.empty() && (t.front() == '+' || t.front() == '-'))
        t.remove_prefix(1);
      if (t.size() > 1 && (t.front() == 'v' || t.front() == 'V' || t.front() == '.') && IsDigit(t[1]))
        t.remove_prefix(1);
      if (t.empty() || !IsDigit(t.front()))
        return false;
      int digits = 0;
      int letters = 0;
      for (const char c: t) {
        if (IsDigit(c))
          digits++;
        else if (std::isalpha(static_cast<unsigned char>(c)))
          letters++;
        else if (std::string_view(".,:;/_+-%'").find(c) == std::string_view::npos)
          return false;
      }
      return letters <= 2 * digits;
    }

    /// An IPv6 address: hex groups and colons, compressed (::) or full.
    bool IsIPv6(const std::string_view t) {
      int colons = 0;
      bool digit = false;
      for (const char c: t) {
        if (c == ':')
          colons++;
        else if (IsDigit(c))
          digit = true;
        else if (!IsHex(c) && c != '.' && c != '/')
          return false;
      }
      return digit && colons >= 2 && (t.find("::") != std::string_view::npos || colons == 7);
    }
  } // namespace

  bool IsSymbolCodePoint(const uint32_t cp) {
    return !IsLetterCodePoint(cp) && !(cp < 0x80 && IsDigit(static_cast<char>(cp)));
  }

  ClassificationResult DetectStructure(const std::string_view text) {
    if (Trim(text).empty())
      return {LanguageEnum::Punctuation, 0.95f};
    size_t pos = 0;
    DecodeCodePoint(text, pos);
    if (pos >= text.size())
      return {LanguageEnum::SingleCharacter, 0.95f};
    return {LanguageEnum::Unknown, 0.0f};
  }

  ClassificationResult DetectPunctuation(const std::string_view text) {
    for (size_t pos = 0; pos < text.size();) {
      if (!IsSymbolCodePoint(DecodeCodePoint(text, pos)))
        return {LanguageEnum::Unknown, 0.0f};
    }
    return {LanguageEnum::Punctuation, 0.90f};
  }

  ClassificationResult DetectNumber(const std::string_view s) {
    const auto t = Trim(s);
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X') &&
        std::ranges::all_of(t.substr(2), IsHex))
      return {LanguageEnum::HexData, 0.90f};
    if (IsIPv6(t))
      return {LanguageEnum::Number, 0.90f};

    /// Several tokens are one number only when each is (a date and a time).
    int tokens = 0;
    size_t pos = 0;
    while (pos < t.size()) {
      const size_t end = std::min(t.find_first_of(" \t\n\r", pos), t.size());
      if (end > pos) {
        if (!IsNumberToken(t.substr(pos, end - pos)))
          return {LanguageEnum::Unknown, 0.0f};
        tokens++;
      }
      pos = end + 1;
    }
    if (tokens == 0 || tokens > 8)
      return {LanguageEnum::Unknown, 0.0f};
    return {LanguageEnum::Number, 0.90f};
  }

  bool DecodeEscapeOnly(const std::string_view s, std::string &decoded) {
    decoded.clear();
    int escapes = 0;
    int32_t high = -1;
    for (size_t i = 0; i < s.size();) {
      const char c = s[i];
      if (c != '\\') {
        /// Between escapes only separators and digits (\u0005_6_7_, <鮄<鮊).
        if (!std::isspace(static_cast<unsigned char>(c)) && !IsDigit(c) &&
            std::string_view(",;<>-_").find(c) == std::string_view::npos)
          return false;
        if (high >= 0)
          AppendUtf8(decoded, 0xFFFD);
        high = -1;
        decoded.push_back(c);
        i++;
        continue;
      }
      if (i + 1 >= s.size())
        return false;
      uint32_t cp = 0;
      size_t len = 0;
      bool byte = false;
      const char k = s[i + 1];
      if (k == 'u' && i + 2 < s.size() && s[i + 2] == '{') {
        const size_t close = s.find('}', i + 3);
        if (close == std::string_view::npos || close == i + 3 || close - i - 3 > 6 ||
            !std::ranges::all_of(s.substr(i + 3, close - i - 3), IsHex))
          return false;
        cp = ParseHex(s.substr(i + 3, close - i - 3));
        len = close + 1 - i;
      } else if ((k == 'u' || k == 'U') && i + (k == 'u' ? 6 : 10) <= s.size() &&
                 std::ranges::all_of(s.substr(i + 2, k == 'u' ? 4 : 8), IsHex)) {
        len = k == 'u' ? 6 : 10;
        cp = ParseHex(s.substr(i + 2, len - 2));
      } else if (k == 'x' && i + 4 <= s.size() && IsHex(s[i + 2]) && IsHex(s[i + 3])) {
        cp = ParseHex(s.substr(i + 2, 2));
        len = 4;
        byte = true;
      } else if (k >= '0' && k <= '7') {
        len = 2;
        while (len < 4 && i + len < s.size() && s[i + len] >= '0' && s[i + len] <= '7')
          len++;
        for (size_t j = 1; j < len; j++)
          cp = cp * 8 + static_cast<uint32_t>(s[i + j] - '0');
        byte = true;
      } else {
        return false;
      }
      escapes++;
      i += len;
      /// \x and octal escapes are bytes; \u escapes are code points, a high
      /// surrogate pairing with the low one that follows.
      if (!byte && cp >= 0xDC00 && cp <= 0xDFFF && high >= 0) {
        AppendUtf8(decoded, 0x10000 + ((static_cast<uint32_t>(high) - 0xD800) << 10) + (cp - 0xDC00));
        high = -1;
        continue;
      }
      if (high >= 0)
        AppendUtf8(decoded, 0xFFFD);
      high = -1;
      if (byte)
        decoded.push_back(static_cast<char>(cp & 0xFF));
      else if (cp >= 0xD800 && cp <= 0xDBFF)
        high = static_cast<int32_t>(cp);
      else
        AppendUtf8(decoded, cp);
    }
    if (high >= 0)
      AppendUtf8(decoded, 0xFFFD);
    return escapes > 0;
  }
} // namespace classifier_internal
