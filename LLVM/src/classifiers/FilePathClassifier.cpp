#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool HasKnownFileExtension(const std::string_view s) {
      const size_t lastSlash = s.find_last_of("/\\");
      const size_t lastDot = s.rfind('.');
      if (lastDot == std::string_view::npos || lastDot == 0)
        return false;
      if (lastSlash != std::string_view::npos && lastDot <= lastSlash + 1)
        return false;

      const auto ext = s.substr(lastDot + 1);
      if (ext.empty() || ext.size() > 12)
        return false;

      static constexpr std::string_view kExtensions[] = {
        "bat", "c", "cc", "cfg", "cmake", "cpp", "css", "dart",
        "def", "frag", "go", "grd", "grdp", "h", "hh", "hpp",
        "htm", "html", "hxx", "idl", "inc", "inl", "ipp", "java",
        "js", "json", "jsx", "kt", "lua", "m", "md", "mm",
        "mojom", "pak", "pb", "php", "pl", "proto", "ps1", "py",
        "rb", "rs", "s", "scss", "sh", "sql", "swift", "textproto",
        "toml", "ts", "tsx", "txt", "vert", "xml", "yaml", "yml",
      };
      return std::ranges::binary_search(kExtensions, ext);
    }

    bool LooksLikeRelativeSourcePath(const std::string_view s) {
      if (s.size() < 4)
        return false;
      if (s.find_first_of(" \t\n\r") != std::string_view::npos)
        return false;

      int separators = 0;
      for (const char c: s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '_' || c == '-' || c == '.' || c == '/' || c == '\\')
          separators += (c == '/' || c == '\\') ? 1 : 0;
        else
          return false;
      }

      return separators >= 1 && HasKnownFileExtension(s);
    }
  } // namespace

  ClassificationResult DetectFilePath(const std::string_view s) {
    if (s.size() < 2)
      return {Language::Unknown, 0.0f};

    if (s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) &&
        s[1] == ':' && (s[2] == '\\' || s[2] == '/'))
      return {Language::FilePath, 0.85f};

    if (s[0] == '/' && s[1] != '*' && s[1] != '/') {
      int slashes = 0;
      for (const char c: s)
        if (c == '/')
          slashes++;
      if (slashes >= 2)
        return {Language::FilePath, 0.80f};
      return {Language::FilePath, 0.55f};
    }

    if (s.starts_with("./") || s.starts_with("../"))
      return {Language::FilePath, 0.75f};

    if (LooksLikeRelativeSourcePath(s))
      return {Language::FilePath, 0.82f};

    return {Language::Unknown, 0.0f};
  }
} // namespace classifier_internal
