//
// Parser.cpp
// Author: Antoine Bastide
// Date: 13/06/2025
//

#include <fstream>
#include <iostream>

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>

#include "../include/Parser.hpp"
#include "../include/MatcherText.hpp"

template<typename T> static void AtomicAdd(std::atomic<T> &dst, T delta) {
  T cur = dst.load(std::memory_order_relaxed);
  while (!dst.compare_exchange_weak(cur, cur + delta, std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

template<typename T> static void AtomicMax(std::atomic<T> &dst, T value) {
  T cur = dst.load(std::memory_order_relaxed);
  while (value > cur && !dst.compare_exchange_weak(cur, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

static bool IsStringToken(const clang::Token &tok) {
  return tok.is(clang::tok::string_literal) ||
         tok.is(clang::tok::wide_string_literal) ||
         tok.is(clang::tok::utf8_string_literal) ||
         tok.is(clang::tok::utf16_string_literal) ||
         tok.is(clang::tok::utf32_string_literal);
}

// Returns the source-form body of one string token.
// Normal strings keep escapes as written.
// Raw strings return verbatim raw body.
static std::string ExtractLiteralBody(std::string_view spelling) {
  const size_t quote = spelling.find('"');
  if (quote == std::string_view::npos)
    return {};

  if (const bool isRaw = quote > 0 && spelling[quote - 1] == 'R'; !isRaw) {
    const size_t end = spelling.rfind('"');
    if (end == std::string_view::npos || end <= quote)
      return {};
    return std::string(spelling.substr(quote + 1, end - quote - 1));
  }

  // Raw form: prefix R"delim(body)delim"
  const size_t open = spelling.find('(', quote);
  if (open == std::string_view::npos)
    return {};

  const std::string delim(spelling.substr(quote + 1, open - quote - 1));
  const std::string suffix = ")" + delim + "\"";
  const size_t close = spelling.rfind(suffix);
  if (close == std::string_view::npos || close <= open)
    return {};

  return std::string(spelling.substr(open + 1, close - open - 1));
}

void Parser::ParseFile(const std::string &path) {
  /// Static compiler infrastructure reused across calls to avoid repeated setup cost.
  static clang::DiagnosticOptions diagOpts;
  static auto diagIDs = llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>();
  static clang::IgnoringDiagConsumer diagConsumer;
  static clang::DiagnosticsEngine diags(diagIDs, diagOpts, &diagConsumer, false);

  /// Language configuration for the lexer.
  static clang::LangOptions langOpts;
  static bool langInitialized = false;
  if (!langInitialized) {
    langOpts.CPlusPlus = true;
    langOpts.CPlusPlus20 = true;
    langInitialized = true;
  }

  /// Shared file manager reused for all parsed files.
  static clang::FileSystemOptions fsOpts;
  static clang::FileManager fileMgr(fsOpts);

  /// Source manager bound to this file.
  clang::SourceManager srcMgr(diags, fileMgr);

  /// Load file contents into a memory buffer.
  auto buffer = fileMgr.getBufferForFile(path);
  if (!buffer) {
    std::cerr << "Failed to open file: " << path << std::endl;
    return;
  }

  const llvm::MemoryBuffer *memBuf = buffer->get();

  /// Register the file inside the source manager.
  clang::FileID fileID = srcMgr.createFileID(std::move(*buffer));
  srcMgr.setMainFileID(fileID);

  /// Construct a raw lexer over the memory buffer.
  const char *bufStart = memBuf->getBufferStart();
  const char *bufEnd = memBuf->getBufferEnd();
  clang::Lexer lexer(srcMgr.getLocForStartOfFile(fileID), langOpts, bufStart, bufStart, bufEnd);
  lexer.SetCommentRetentionState(true);

  /// Tokenize the file until EOF.
  clang::Token tok{};
  while (true) {
    lexer.LexFromRawLexer(tok);
    if (tok.is(clang::tok::eof))
      break;

    /// Handle string literal tokens.
    if (IsStringToken(tok)) {
      std::string value;
      clang::Token current = tok;

      /// Concatenate adjacent string literals ("a" "b").
      do {
        const std::string spelling = clang::Lexer::getSpelling(current, srcMgr, langOpts);
        value += ExtractLiteralBody(spelling);
        lexer.LexFromRawLexer(current);
      } while (IsStringToken(current));

      tok = current;
      process(std::move(value), STRING_STATS);
      continue;
    }

    /// Capture comment tokens.
    if (tok.is(clang::tok::comment)) {
      std::string comment = clang::Lexer::getSpelling(tok, srcMgr, langOpts);
      process(std::move(comment), DOCS_STATS);
    }
  }
}

void Parser::process(std::string &&string, EmbeddedStats &stats) {
  uint64_t toothpicks = 0;
  for (const unsigned char c: string) {
    if (c == '\\')
      ++toothpicks;
  }

  const auto [unmatched, maxDepth, rawChars] = AnalyzeMatcherText(string);

  AtomicAdd(stats.count, 1.0);
  AtomicAdd(stats.rawChars, static_cast<double>(rawChars));

  if (toothpicks > 0)
    AtomicAdd(stats.withToothpicks, 1.0);
  AtomicAdd(stats.toothpicks, static_cast<double>(toothpicks));
  AtomicMax(stats.toothpicksMax, static_cast<double>(toothpicks));

  if (unmatched > 0)
    AtomicAdd(stats.withNonCompliance, 1.0);
  AtomicAdd(stats.nonComplianceCount, static_cast<double>(unmatched));
  AtomicMax(stats.nonComplianceMax, static_cast<double>(unmatched));

  if (maxDepth > 1)
    AtomicAdd(stats.withNesting, 1.0);
  AtomicAdd(stats.nestingDepthTotal, static_cast<double>(maxDepth));
  AtomicMax(stats.nestingDepthMax, static_cast<double>(maxDepth));
}
