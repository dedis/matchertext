#ifndef TREE_SITTER_PARSER_HPP
#define TREE_SITTER_PARSER_HPP

#include <string>

#include "JSON.hpp"
#include "LanguageClassifier.hpp"

/// In-process tree-sitter string/comment extractor. Counterpart of
/// Parser::ParseC_CPP for the ~21 programming languages whose grammars are
/// linked into the binary (see extern/tree-sitter). Emits the same JSON
/// contract: an array of {"kind":"string"|"comment","value":"..."}.
namespace TreeSitter {
  /// True if `language` is parsed in-process via a linked tree-sitter grammar.
  bool IsTreeSitterLanguage(LanguageEnum language);

  /// Parse `path` for `language`, appending {"kind","value"} objects to
  /// `result` (made an array if it isn't one). Thread-safe (thread_local
  /// parser). Returns false only on unreadable file / unsupported language;
  /// parse errors yield whatever was collected (possibly empty).
  bool Parse(LanguageEnum language, const std::string &path, Serde::JSON &result);
}

#endif // TREE_SITTER_PARSER_HPP
