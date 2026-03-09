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
    if (tok.is(clang::tok::string_literal) ||
        tok.is(clang::tok::wide_string_literal) ||
        tok.is(clang::tok::utf8_string_literal) ||
        tok.is(clang::tok::utf16_string_literal) ||
        tok.is(clang::tok::utf32_string_literal)) {
      std::string combined;
      clang::Token current = tok;

      /// Concatenate adjacent string literals ("a" "b").
      do {
        combined += clang::Lexer::getSpelling(current, srcMgr, langOpts);
        lexer.LexFromRawLexer(current);
      } while (current.is(clang::tok::string_literal) ||
               current.is(clang::tok::wide_string_literal) ||
               current.is(clang::tok::utf8_string_literal) ||
               current.is(clang::tok::utf16_string_literal) ||
               current.is(clang::tok::utf32_string_literal));

      tok = current;

      /// Extract the literal contents from the token spelling.
      std::string value;
      if (size_t start = combined.find('"'); start != std::string::npos) {
        if (start > 0 && combined[start - 1] == 'R') {
          /// Raw string: R"delim(content)delim"
          size_t open = combined.find('(', start);
          if (size_t close = combined.rfind(')');
            open != std::string::npos && close != std::string::npos && close > open)
            value = combined.substr(open + 1, close - open - 1);
        } else {
          /// Normal quoted string.
          if (size_t end = combined.rfind('"'); end > start)
            value = combined.substr(start + 1, end - start - 1);
        }
      }

      processString(std::move(value));
      continue;
    }

    /// Capture comment tokens.
    if (tok.is(clang::tok::comment)) {
      std::string comment = clang::Lexer::getSpelling(tok, srcMgr, langOpts);
      processString(std::move(comment));
    }
  }
}

void Parser::processString(std::string &&string) {
  // Do something here
}
