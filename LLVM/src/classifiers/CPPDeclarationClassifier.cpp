#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsTypeToken(const std::string_view token) {
      if (token.empty() || token.size() > 64)
        return false;

      bool hasAlpha = false;
      for (size_t i = 0; i < token.size(); i++) {
        const unsigned char uc = static_cast<unsigned char>(token[i]);
        if (std::isalpha(uc)) {
          hasAlpha = true;
          continue;
        }
        if (std::isdigit(uc) || token[i] == '_' || token[i] == ':' ||
            token[i] == '<' || token[i] == '>' || token[i] == ',')
          continue;
        return false;
      }
      return hasAlpha;
    }

    bool IsIdentifierToken(const std::string_view token) {
      if (token.empty() || token.size() > 64)
        return false;
      if (!std::isalpha(static_cast<unsigned char>(token.front())) &&
          token.front() != '_')
        return false;

      for (const char c: token) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_')
          return false;
      }
      return true;
    }
  } // namespace

  ClassificationResult DetectCPPDeclarationFragment(const std::string_view s) {
    const auto trimmed = Trim(s);
    if (trimmed.size() < 6 || trimmed.size() > 96)
      return {Language::Unknown, 0.0f};

    if (trimmed.find_first_of("{}[]();=,") != std::string_view::npos)
      return {Language::Unknown, 0.0f};
    if (trimmed.find("::") != std::string_view::npos ||
        trimmed.find("->") != std::string_view::npos ||
        trimmed.find("=>") != std::string_view::npos)
      return {Language::Unknown, 0.0f};

    size_t pos = 0;
    std::string_view first;
    std::string_view second;
    while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos])))
      pos++;
    const size_t firstEnd = trimmed.find_first_of(" \t\n\r", pos);
    if (firstEnd == std::string_view::npos)
      return {Language::Unknown, 0.0f};
    first = trimmed.substr(pos, firstEnd - pos);

    pos = trimmed.find_first_not_of(" \t\n\r", firstEnd);
    if (pos == std::string_view::npos)
      return {Language::Unknown, 0.0f};
    second = trimmed.substr(pos);

    if (!IsTypeToken(first))
      return {Language::Unknown, 0.0f};

    size_t refCount = 0;
    while (refCount < second.size() &&
           (second[refCount] == '&' || second[refCount] == '*'))
      refCount++;
    if (refCount == 0 || refCount > 2)
      return {Language::Unknown, 0.0f};

    const auto identifier = Trim(second.substr(refCount));
    if (!IsIdentifierToken(identifier))
      return {Language::Unknown, 0.0f};

    return {Language::CPP, refCount == 2 ? 0.88f : 0.82f};
  }
} // namespace classifier_internal
