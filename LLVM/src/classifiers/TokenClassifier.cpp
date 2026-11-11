#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsTokenSeparator(const char c) {
      return c == '_' || c == '-' || c == '.' || c == ':' || c == '/' || c == '+' || c == '#';
    }

    bool IsSentencePunctuation(const char c) {
      return c == ':' || c == '.' || c == ',' || c == '!' || c == '?';
    }

    /// A bare file name such as config.yml: a name, a dot, and a known extension.
    bool LooksLikeFileName(const std::string_view s) {
      const size_t dot = s.rfind('.');
      if (dot == 0 || dot == std::string_view::npos || s.find('/') != std::string_view::npos ||
          s.find(':') != std::string_view::npos)
        return false;
      const auto ext = s.substr(dot + 1);
      static constexpr std::string_view kExtensions[] = {
        "bak", "bat", "bin", "bmp", "c", "cc", "cfg", "class", "cmake", "conf", "cpp", "cs",
        "css", "csv", "dart", "dat", "db", "dll", "doc", "docx", "dylib", "exe", "gif", "go",
        "gz", "h", "hpp", "htm", "html", "ico", "ini", "jar", "java", "jpeg", "jpg", "js",
        "json", "jsx", "kt", "lock", "log", "lua", "md", "mjs", "mp3", "mp4", "pdf", "php",
        "pl", "plist", "png", "proto", "py", "rb", "rs", "scss", "sh", "so", "sql", "svg",
        "swift", "tar", "tgz", "tmp", "toml", "ts", "tsx", "txt", "wasm", "wav", "webp", "xml",
        "yaml", "yml", "zip",
      };
      return std::ranges::binary_search(kExtensions, ext);
    }
  } // namespace

  ClassificationResult DetectWordOrToken(const std::string_view s) {
    auto t = Trim(s);
    if (t.size() < 2 || t.find_first_of(" \t\n\r") != std::string_view::npos)
      return {LanguageEnum::Unknown, 0.0f};

    /// #include is a C directive; #id and #e9c7cd are a CSS selector and color.
    if (t.front() == '#') {
      const auto name = t.substr(1);
      static constexpr std::string_view kDirectives[] = {
        "define", "elif", "else", "endif", "error", "if", "ifdef", "ifndef",
        "import", "include", "line", "pragma", "undef", "warning",
      };
      if (std::ranges::binary_search(kDirectives, name))
        return {LanguageEnum::C, 0.85f};
      const bool hexColor = (name.size() == 3 || name.size() == 4 || name.size() == 6 || name.size() == 8) &&
                            std::ranges::all_of(name, [](const char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
      const bool id = !name.empty() && !std::isdigit(static_cast<unsigned char>(name.front())) &&
                      std::ranges::all_of(name, [](const char c) {
                        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
                      });
      if (hexColor || id)
        return {LanguageEnum::CSS, 0.80f};
      return {LanguageEnum::Unknown, 0.0f};
    }
    /// A run of one separator (o-----o, a___b) is a rule line, not a name.
    for (size_t i = 2; i < t.size(); i++)
      if (IsTokenSeparator(t[i]) && t[i] == t[i - 1] && t[i] == t[i - 2])
        return {LanguageEnum::Unknown, 0.0f};

    /// A word followed by sentence punctuation (Error:, Done.) is still a word,
    /// but a scope (std::) is not.
    auto word = t;
    while (word.size() > 1 && IsSentencePunctuation(word.back()) && !word.ends_with("::"))
      word.remove_suffix(1);

    bool lettersOnly = true;
    int letters = 0;
    int alternations = 0; // letter-to-digit and digit-to-letter changes
    int previous = 0;     // 1 letter, 2 digit
    for (size_t pos = 0; pos < word.size();) {
      const char c = word[pos];
      const uint32_t cp = DecodeCodePoint(word, pos);
      int kind = 0;
      if (IsLetterCodePoint(cp)) {
        letters++;
        kind = 1;
      } else if (cp < 0x80 && std::isdigit(static_cast<unsigned char>(c))) {
        kind = 2;
      } else if (!(cp < 0x80 && IsTokenSeparator(c))) {
        return {LanguageEnum::Unknown, 0.0f};
      }
      if (kind != 1)
        lettersOnly = false;
      if (kind != 0 && previous != 0 && kind != previous)
        alternations++;
      previous = kind;
    }
    bool camelCase = false;
    for (size_t i = 1; i < word.size(); i++)
      if (std::islower(static_cast<unsigned char>(word[i - 1])) && std::isupper(static_cast<unsigned char>(word[i])))
        camelCase = true;

    if (letters == 0)
      return {LanguageEnum::Unknown, 0.0f};

    /// A run of one repeated letter (aaaaaa) is filler, not a word.
    if (lettersOnly) {
      int run = 1;
      for (size_t i = 1; i < word.size(); i++) {
        run = word[i] == word[i - 1] ? run + 1 : 1;
        if (run >= 5)
          return {LanguageEnum::Unknown, 0.0f};
      }
    }
    /// A word is plain text; camelCase and PascalCase names are identifiers.
    if (lettersOnly && !camelCase)
      return {LanguageEnum::PlainText, 0.90f};

    /// Tokens: identifiers, keys, and package paths. Numbers (50px, 2.4.1) are
    /// detected before; encoded blobs such as base64, whose letters and digits
    /// keep alternating, stay unclassified.
    if (t.find("//") != std::string_view::npos)
      return {LanguageEnum::Unknown, 0.0f};
    if (LooksLikeFileName(t))
      return {LanguageEnum::FilePath, 0.80f};
    /// Mangled C++ symbols (_ZN4llvm..., _CPPv4...) alternate like blobs but are names.
    if (t.starts_with("_Z") || t.starts_with("_CPP"))
      return {LanguageEnum::IdentifierLike, 0.90f};
    if (alternations >= 6)
      return {LanguageEnum::Unknown, 0.0f};
    return {LanguageEnum::IdentifierLike, 0.90f};
  }
} // namespace classifier_internal
