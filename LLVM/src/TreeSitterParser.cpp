#include "../include/TreeSitterParser.hpp"

#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <tree_sitter/api.h>

// Each grammar's language function lives in its vendored parser.c.
extern "C" {
  const TSLanguage *tree_sitter_javascript(void);
  const TSLanguage *tree_sitter_typescript(void);
  const TSLanguage *tree_sitter_java(void);
  const TSLanguage *tree_sitter_c_sharp(void);
  const TSLanguage *tree_sitter_rust(void);
  const TSLanguage *tree_sitter_ruby(void);
  const TSLanguage *tree_sitter_php(void);
  const TSLanguage *tree_sitter_perl(void);
  const TSLanguage *tree_sitter_lua(void);
  const TSLanguage *tree_sitter_swift(void);
  const TSLanguage *tree_sitter_kotlin(void);
  const TSLanguage *tree_sitter_r(void);
  const TSLanguage *tree_sitter_scala(void);
  const TSLanguage *tree_sitter_haskell(void);
  const TSLanguage *tree_sitter_ocaml(void);
  const TSLanguage *tree_sitter_erlang(void);
  const TSLanguage *tree_sitter_elixir(void);
  const TSLanguage *tree_sitter_dart(void);
  const TSLanguage *tree_sitter_objc(void);
  const TSLanguage *tree_sitter_glsl(void);
  const TSLanguage *tree_sitter_hlsl(void);
  const TSLanguage *tree_sitter_go(void);
  const TSLanguage *tree_sitter_python(void);
}

namespace {
  // ---- grammar registry ----------------------------------------------------
  const TSLanguage *languageFor(const LanguageEnum lang) {
    switch (lang) {
      case LanguageEnum::JavaScript: return tree_sitter_javascript();
      case LanguageEnum::TypeScript: return tree_sitter_typescript();
      case LanguageEnum::Java: return tree_sitter_java();
      case LanguageEnum::CSharp: return tree_sitter_c_sharp();
      case LanguageEnum::Rust: return tree_sitter_rust();
      case LanguageEnum::Ruby: return tree_sitter_ruby();
      case LanguageEnum::PHP: return tree_sitter_php();
      case LanguageEnum::Perl: return tree_sitter_perl();
      case LanguageEnum::Lua: return tree_sitter_lua();
      case LanguageEnum::Swift: return tree_sitter_swift();
      case LanguageEnum::Kotlin: return tree_sitter_kotlin();
      case LanguageEnum::R: return tree_sitter_r();
      case LanguageEnum::Scala: return tree_sitter_scala();
      case LanguageEnum::Haskell: return tree_sitter_haskell();
      case LanguageEnum::OCaml: return tree_sitter_ocaml();
      case LanguageEnum::Erlang: return tree_sitter_erlang();
      case LanguageEnum::Elixir: return tree_sitter_elixir();
      case LanguageEnum::Dart: return tree_sitter_dart();
      case LanguageEnum::Objective_C: return tree_sitter_objc();
      case LanguageEnum::GLSL: return tree_sitter_glsl();
      case LanguageEnum::HLSL: return tree_sitter_hlsl();
      case LanguageEnum::Go: return tree_sitter_go();
      case LanguageEnum::Python: return tree_sitter_python();
      default: return nullptr;
    }
  }

  // ---- node-type classification --------------------------------------------
  enum class Kind { Other, String, Comment };

  struct Classifier {
    std::unordered_set<std::string_view> stringTypes;
    std::unordered_set<std::string_view> commentTypes;

    Kind classify(const char *type) const {
      const std::string_view t(type);
      if (commentTypes.contains(t))
        return Kind::Comment;
      if (stringTypes.contains(t))
        return Kind::String;
      // Heuristic fallback — resilient to grammar version drift and the many
      // string/comment node-type spellings across grammars.
      if (t.find("comment") != std::string_view::npos)
        return Kind::Comment;
      if (t.find("string") != std::string_view::npos || t == "char" || t.find("char_literal") != std::string_view::npos)
        return Kind::String;
      return Kind::Other;
    }
  };

  // Per-language overrides for node types the heuristic misses (no "string"/
  // "comment" substring). Languages absent here rely purely on the heuristic.
  const Classifier &classifierFor(const LanguageEnum lang) {
    static const std::unordered_map<LanguageEnum, Classifier> table = [] {
      std::unordered_map<LanguageEnum, Classifier> m;
      m[LanguageEnum::Java] = {{"text_block"}, {}};
      m[LanguageEnum::Ruby] = {{"heredoc_body", "heredoc_beginning", "bare_string"}, {}};
      m[LanguageEnum::PHP] = {{"heredoc", "nowdoc", "encapsed_string"}, {}};
      m[LanguageEnum::Perl] = {{"heredoc_content"}, {"=pod"}};
      m[LanguageEnum::Elixir] = {{"charlist", "sigil", "quoted_content"}, {}};
      m[LanguageEnum::Erlang] = {{"sigil"}, {}};
      m[LanguageEnum::OCaml] = {{"quoted_string"}, {}};
      m[LanguageEnum::Haskell] = {{}, {"haddock"}};
      return m;
    }();
    static const Classifier kEmpty{};
    const auto it = table.find(lang);
    return it == table.end() ? kEmpty : it->second;
  }

  // ---- text helpers --------------------------------------------------------
  // A node's source bytes as a view into `src` (no copy).
  std::string_view nodeText(const TSNode node, const std::string_view src) {
    uint32_t s = ts_node_start_byte(node);
    uint32_t e = ts_node_end_byte(node);
    if (e > src.size())
      e = static_cast<uint32_t>(src.size());
    if (s > e)
      s = e;
    return src.substr(s, e - s);
  }

  bool isQuote(const char c) { return c == '"' || c == '\'' || c == '`'; }

  // Strip surrounding string delimiters (and optional letter/@/$ prefixes),
  // handling triple-quote runs. Fallback when the grammar exposes no content
  // child. Returns a view into the input. Mirrors parser.py / parser.go.
  std::string_view stripDelimiters(const std::string_view s) {
    size_t b = 0;
    const size_t e = s.size();
    while (b < e && !isQuote(s[b]) && (std::isalpha(static_cast<unsigned char>(s[b])) || s[b] == '@' || s[b] == '$'))
      ++b;
    if (b >= e || !isQuote(s[b]))
      return s; // no recognizable quote delimiter
    const char q = s[b];
    size_t open = 0;
    while (b + open < e && s[b + open] == q)
      ++open;
    size_t close = 0;
    while (close < e - b && s[e - 1 - close] == q)
      ++close;
    const size_t take = std::min(open, close);
    const size_t bodyStart = b + take;
    const size_t bodyEnd = e >= take && e - take >= bodyStart ? e - take : bodyStart;
    return s.substr(bodyStart, bodyEnd - bodyStart);
  }

  // Body of a string node, returned as a view. Prefer content/fragment children
  // (delimiters and interpolation markers excluded for free); otherwise strip
  // delimiters off the whole node. The rare multi-child case concatenates into
  // `scratch` (reused across nodes) and returns a view into it.
  std::string_view stringBody(const TSNode node, const std::string_view src, std::string &scratch) {
    const uint32_t n = ts_node_named_child_count(node);
    int firstContent = -1;
    int contentCount = 0;
    for (uint32_t i = 0; i < n; ++i) {
      const char *t = ts_node_type(ts_node_named_child(node, i));
      if (std::strstr(t, "content") || std::strstr(t, "fragment")) {
        if (firstContent < 0)
          firstContent = static_cast<int>(i);
        ++contentCount;
      }
    }
    if (contentCount == 1)
      return nodeText(ts_node_named_child(node, static_cast<uint32_t>(firstContent)), src);
    if (contentCount > 1) {
      scratch.clear();
      for (uint32_t i = 0; i < n; ++i) {
        const TSNode c = ts_node_named_child(node, i);
        const char *t = ts_node_type(c);
        if (std::strstr(t, "content") || std::strstr(t, "fragment"))
          scratch.append(nodeText(c, src));
      }
      return scratch;
    }
    return stripDelimiters(nodeText(node, src));
  }

  Serde::JSON makeObj(const char *kind, const std::string_view value) {
    return Serde::JSON::Object({{"kind", Serde::JSON(kind)}, {"value", Serde::JSON(value)}});
  }

  // ---- tree walk -----------------------------------------------------------
  // Iterative (explicit stack) to avoid recursion-depth blowups on deep trees.
  // String/comment nodes are emitted whole and not descended into; ERROR nodes
  // are descended through so well-formed descendants are still collected.
  void walk(const TSNode root, const std::string_view src, const Classifier &cls, Serde::JSON &out) {
    std::vector<TSNode> stack;
    std::string scratch; // reused buffer for multi-child string concatenation
    stack.push_back(root);
    while (!stack.empty()) {
      const TSNode node = stack.back();
      stack.pop_back();

      switch (cls.classify(ts_node_type(node))) {
        case Kind::Comment:
          out.PushBack(makeObj("comment", nodeText(node, src)));
          continue;
        case Kind::String:
          out.PushBack(makeObj("string", stringBody(node, src, scratch)));
          continue;
        case Kind::Other:
          break;
      }

      const uint32_t count = ts_node_child_count(node);
      for (uint32_t i = count; i-- > 0;)
        stack.push_back(ts_node_child(node, i));
    }
  }

  // Cap per-file parse time so a pathological input (e.g. a huge minified
  // bundle, common in large repos) can't wedge a worker thread forever.
  // tree-sitter invokes the progress callback periodically while parsing;
  // returning true cancels the parse, which then yields a null tree.
  constexpr auto kParseTimeout = std::chrono::seconds(5);

  // TSInput read callback: hand tree-sitter the whole remaining buffer at once.
  const char *readString(void *payload, const uint32_t byte, TSPoint, uint32_t *bytesRead) {
    const auto *s = static_cast<const std::string *>(payload);
    if (byte >= s->size()) {
      *bytesRead = 0;
      return "";
    }
    *bytesRead = static_cast<uint32_t>(s->size() - byte);
    return s->data() + byte;
  }

  bool parseExpired(TSParseState *state) {
    const auto *deadline = static_cast<const std::chrono::steady_clock::time_point *>(state->payload);
    return std::chrono::steady_clock::now() >= *deadline;
  }

  // thread_local TSParser, cleaned up at thread exit (parsing runs under OpenMP).
  struct ParserHolder {
    TSParser *parser = nullptr;
    TSParser *get() {
      if (!parser)
        parser = ts_parser_new();
      return parser;
    }
    ~ParserHolder() {
      if (parser)
        ts_parser_delete(parser);
    }
  };
}

bool TreeSitter::IsTreeSitterLanguage(const LanguageEnum language) {
  return languageFor(language) != nullptr;
}

bool TreeSitter::Parse(const LanguageEnum language, const std::string &path, Serde::JSON &result) {
  const TSLanguage *tsLang = languageFor(language);
  if (!tsLang)
    return false;

  // Bulk read (one allocation + one read) instead of istreambuf_iterator.
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    return false;
  const std::streamoff size = in.tellg();
  if (size < 0)
    return false;
  std::string src(static_cast<size_t>(size), '\0');
  in.seekg(0);
  in.read(src.data(), size);
  src.resize(static_cast<size_t>(in.gcount()));

  if (!result.IsArray())
    result = Serde::JSON::Array();

  // Skip binary files. Source extensions collide with non-source formats (e.g.
  // `.ts` is both TypeScript and MPEG transport stream, which Chromium ships as
  // test data). A NUL byte is a reliable binary signal — source essentially
  // never contains one — and feeding binary to the GLR parser triggers
  // pathological error-recovery blowups the parse timeout can't always catch.
  if (std::memchr(src.data(), '\0', src.size()) != nullptr)
    return true;

  thread_local ParserHolder holder;
  TSParser *parser = holder.get();
  if (!ts_parser_set_language(parser, tsLang))
    return false; // ABI mismatch between grammar and core runtime

  auto deadline = std::chrono::steady_clock::now() + kParseTimeout;
  TSInput input{};
  input.payload = const_cast<std::string *>(&src);
  input.read = readString;
  input.encoding = TSInputEncodingUTF8;
  input.decode = nullptr;
  TSParseOptions opts{};
  opts.payload = &deadline;
  opts.progress_callback = parseExpired;

  TSTree *tree = ts_parser_parse_with_options(parser, nullptr, input, opts);
  if (!tree)
    return true; // timed out or nothing parsed; empty array is valid

  walk(ts_tree_root_node(tree), src, classifierFor(language), result);
  ts_tree_delete(tree);
  return true;
}
