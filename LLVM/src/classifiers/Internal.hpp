#pragma once

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>
#include "../../include/LanguageClassifier.hpp"

namespace classifier_internal {
  inline char ToLower(const char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  inline std::string_view TrimLeft(const std::string_view s) {
    const size_t pos = s.find_first_not_of(" \t\n\r");
    return pos == std::string_view::npos ? std::string_view{} : s.substr(pos);
  }

  inline std::string_view TrimRight(const std::string_view s) {
    const size_t pos = s.find_last_not_of(" \t\n\r");
    return pos == std::string_view::npos ? std::string_view{} : s.substr(0, pos + 1);
  }

  inline std::string_view Trim(const std::string_view s) {
    return TrimRight(TrimLeft(s));
  }

  inline bool StartsWithCI(const std::string_view s, const std::string_view prefix) {
    if (s.size() < prefix.size())
      return false;
    for (size_t i = 0; i < prefix.size(); i++) {
      if (ToLower(s[i]) != ToLower(prefix[i]))
        return false;
    }
    return true;
  }

  inline size_t FindCI(const std::string_view haystack, const std::string_view needle) {
    if (needle.empty())
      return 0;
    if (needle.size() > haystack.size())
      return std::string_view::npos;
    const size_t last = haystack.size() - needle.size();
    for (size_t i = 0; i <= last; i++) {
      bool found = true;
      for (size_t j = 0; j < needle.size(); j++) {
        if (ToLower(haystack[i + j]) != ToLower(needle[j])) {
          found = false;
          break;
        }
      }
      if (found)
        return i;
    }
    return std::string_view::npos;
  }

  inline std::string StripCommentDecorators(const std::string_view body) {
    std::string cleaned;
    cleaned.reserve(body.size());

    size_t pos = 0;
    bool wroteLine = false;
    while (pos <= body.size()) {
      const size_t end = body.find('\n', pos);
      const size_t lineEnd = end == std::string_view::npos ? body.size() : end;
      auto line = Trim(body.substr(pos, lineEnd - pos));

      if (line.starts_with("/*"))
        line = TrimLeft(line.substr(2));
      if (line.ends_with("*/"))
        line = TrimRight(line.substr(0, line.size() - 2));
      if (line.starts_with("///"))
        line = TrimLeft(line.substr(3));
      else if (line.starts_with("//"))
        line = TrimLeft(line.substr(2));
      else if (line.starts_with('*'))
        line = TrimLeft(line.substr(1));
      else if ((line.starts_with("# ") || line.starts_with("#\t")) &&
               !line.starts_with("#!"))
        line = TrimLeft(line.substr(1));
      else if (line.starts_with("<!--"))
        line = TrimLeft(line.substr(4));

      if (line.ends_with("-->"))
        line = TrimRight(line.substr(0, line.size() - 3));

      if (!line.empty()) {
        if (wroteLine)
          cleaned.push_back('\n');
        cleaned.append(line);
        wroteLine = true;
      }

      if (end == std::string_view::npos)
        break;
      pos = end + 1;
    }

    if (!cleaned.empty())
      return cleaned;
    return std::string(Trim(body));
  }

  inline std::string UnescapeCommonSequences(const std::string_view body) {
    std::string result;
    result.reserve(body.size());

    for (size_t i = 0; i < body.size(); i++) {
      if (body[i] != '\\' || i + 1 >= body.size()) {
        result.push_back(body[i]);
        continue;
      }

      switch (body[i + 1]) {
        case '\\':
          result.push_back('\\');
          i++;
          break;
        case '"':
          result.push_back('"');
          i++;
          break;
        case '\'':
          result.push_back('\'');
          i++;
          break;
        case 'n':
          result.push_back('\n');
          i++;
          break;
        case 'r':
          result.push_back('\r');
          i++;
          break;
        case 't':
          result.push_back('\t');
          i++;
          break;
        default:
          result.push_back(body[i]);
          break;
      }
    }

    return result;
  }

  inline std::string NormalizeForClassification(const std::string_view body) {
    return UnescapeCommonSequences(StripCommentDecorators(body));
  }

  struct YAMLAnalysis {
    int docMarkers = 0;
    int keyValueLines = 0;
    int blockKeyLines = 0;
    int listLines = 0;
    int nonEmptyLines = 0;
  };

  YAMLAnalysis AnalyzeYAMLStructure(std::string_view s);
  bool HasStrongYAMLEvidence(const YAMLAnalysis &analysis);
  bool HasStrongYAMLEvidence(std::string_view s);

  struct BinaryDataAnalysis {
    int controlBytes = 0;
    int escapedBytes = 0;
    int hexEscapes = 0;
    int octalEscapes = 0;
    int nullEscapes = 0;
    int letters = 0;
    int spaces = 0;
    int visiblePrintableChars = 0;

    [[nodiscard]] int StrongEscapes() const {
      return hexEscapes + octalEscapes + nullEscapes;
    }

    [[nodiscard]] float ControlRatio(const size_t size) const {
      return static_cast<float>(controlBytes) /
             static_cast<float>(std::max<size_t>(size, 1));
    }

    [[nodiscard]] float EscapeRatio(const size_t size) const {
      return static_cast<float>(escapedBytes * 4) /
             static_cast<float>(std::max<size_t>(size, 1));
    }
  };

  BinaryDataAnalysis AnalyzeBinaryData(std::string_view s);
  bool HasStrongBinaryEvidence(const BinaryDataAnalysis &analysis, std::string_view s);
  bool HasReadableBinaryContext(const BinaryDataAnalysis &analysis);

  bool LooksLikeShortPlainTextLabel(std::string_view s);
  bool HasRepeatedNonLetterRun(std::string_view s, int *maxRunLength = nullptr);
  bool LooksLikeBareDomainLikeToken(std::string_view s);

  ClassificationResult DetectURL(std::string_view s);
  ClassificationResult DetectEmail(std::string_view s);
  ClassificationResult DetectFilePath(std::string_view s);
  ClassificationResult DetectFormatString(std::string_view s);
  ClassificationResult DetectInlineAsm(std::string_view s);
  ClassificationResult DetectPseudoBinaryData(std::string_view s);
  ClassificationResult DetectBinaryData(std::string_view s);
  ClassificationResult DetectHexData(std::string_view s);
  ClassificationResult DetectJSON(std::string_view s);
  ClassificationResult DetectYAML(std::string_view s);
  ClassificationResult DetectSQL(std::string_view s);
  ClassificationResult DetectHTML(std::string_view s);
  ClassificationResult DetectXML(std::string_view s);
  ClassificationResult DetectRegex(std::string_view s);
  ClassificationResult DetectCSS(std::string_view s);
  ClassificationResult DetectShell(std::string_view s);
  ClassificationResult DetectCPPDeclarationFragment(std::string_view s);
  ClassificationResult DetectPlainText(std::string_view s);
  ClassificationResult DetectSeparatorLine(std::string_view s);
} // namespace classifier_internal
