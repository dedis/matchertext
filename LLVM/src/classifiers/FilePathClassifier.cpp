#include "Internal.hpp"

namespace classifier_internal {

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

  return {Language::Unknown, 0.0f};
}

} // namespace classifier_internal
