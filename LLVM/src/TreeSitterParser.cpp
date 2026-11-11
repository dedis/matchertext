#include "../include/TreeSitterParser.hpp"

#include <algorithm>
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

#include "../include/RawRewrite.hpp"

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

  // ---- escape rules ---------------------------------------------------------
  // Where a grammar puts escape sequences: as nodes, or inside one text token that
  // a lexer must split. Ruby single-quoted and Swift extended-delimiter strings
  // have no escape nodes but still contain escapes. The Perl grammar marks only the
  // first two characters of an escape (\x of \xe9).
  enum class EscapeSource : uint8_t { Nodes, Kotlin, Erlang, Haskell, Perl, Ruby, Swift };

  struct EscapeRules {
    RawRewrite::Dialect dialect;
    EscapeSource source = EscapeSource::Nodes;
  };

  const EscapeRules &escapeRulesFor(const LanguageEnum lang) {
    static const std::unordered_map<LanguageEnum, EscapeRules> table = [] {
      std::unordered_map<LanguageEnum, EscapeRules> m;
      using enum RawRewrite::Dialect::Digits;
      const RawRewrite::Dialect c{.controls = "abefnrtv", .byteEscapes = true};
      m[LanguageEnum::JavaScript] = {{.controls = "bfnrtv"}};
      m[LanguageEnum::TypeScript] = {{.controls = "bfnrtv"}};
      m[LanguageEnum::Java] = {{.controls = "bfnrt"}};
      m[LanguageEnum::CSharp] = {{.controls = "abefnrtv"}};
      m[LanguageEnum::Rust] = {{.controls = "nrt"}};
      m[LanguageEnum::Ruby] = {{.controls = "abefnrtv", .controlLetters = true, .byteEscapes = true}, EscapeSource::Ruby};
      m[LanguageEnum::PHP] = {{.controls = "efnrtv", .unknownKeepsBackslash = true, .byteEscapes = true}};
      m[LanguageEnum::Perl] = {{.controls = "abefnrt", .controlLetters = true, .byteEscapes = true}, EscapeSource::Perl};
      m[LanguageEnum::Lua] = {{.controls = "abfnrtv", .empty = "z", .digits = Decimal, .byteEscapes = true}};
      m[LanguageEnum::Swift] = {{.controls = "nrt"}, EscapeSource::Swift};
      m[LanguageEnum::Kotlin] = {{.controls = "bnrt"}, EscapeSource::Kotlin};
      m[LanguageEnum::R] = {{.controls = "abfnrtv", .byteEscapes = true}};
      m[LanguageEnum::Scala] = {{.controls = "bfnrt"}};
      m[LanguageEnum::Haskell] = {{.controls = "abfnrtv", .empty = "&", .digits = Decimal}, EscapeSource::Haskell};
      m[LanguageEnum::OCaml] = {{.controls = "bnrt", .digits = Decimal, .unknownKeepsBackslash = true, .byteEscapes = true}};
      m[LanguageEnum::Erlang] = {{.controls = "bdefnrtv"}, EscapeSource::Erlang};
      m[LanguageEnum::Elixir] = {{.controls = "abdefnrtv", .byteEscapes = true}};
      m[LanguageEnum::Dart] = {{.controls = "bfnrtv", .digits = None}};
      m[LanguageEnum::Objective_C] = {c};
      m[LanguageEnum::GLSL] = {c};
      m[LanguageEnum::HLSL] = {c};
      m[LanguageEnum::Go] = {{.controls = "abfnrtv", .byteEscapes = true}};
      m[LanguageEnum::Python] = {{.controls = "abfnrtv"}};
      return m;
    }();
    return table.at(lang);
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

  // Source byte range [start, end).
  struct Span {
    uint32_t start, end;
  };

  bool isContent(const char *type) {
    return std::strstr(type, "content") || std::strstr(type, "fragment");
  }

  // Escape nodes across the vendored grammars; a text check skips Python's `{{`.
  bool isEscape(const TSNode node, const std::string_view src) {
    const std::string_view t(ts_node_type(node));
    return (t == "escape_sequence" || t == "str_escaped_char" || t == "character_escape_seq") &&
           nodeText(node, src).starts_with('\\');
  }

  Span spanOf(const TSNode node) {
    return {ts_node_start_byte(node), ts_node_end_byte(node)};
  }

  // Body of a string node as source spans in order. Content/fragment children
  // exclude delimiters and interpolations; escape and ERROR nodes beside them
  // belong to the body too. Without content children, the body is the node text without its
  // delimiters.
  void bodySpans(const TSNode node, const std::string_view src, std::vector<Span> &spans) {
    spans.clear();
    const uint32_t n = ts_node_named_child_count(node);
    bool content = false;
    for (uint32_t i = 0; i < n && !content; ++i)
      content = isContent(ts_node_type(ts_node_named_child(node, i)));
    if (content) {
      for (uint32_t i = 0; i < n; ++i) {
        const TSNode c = ts_node_named_child(node, i);
        // ERROR children hold text the grammar rejects, such as OCaml's \d.
        if (isContent(ts_node_type(c)) || isEscape(c, src) || ts_node_is_error(c))
          spans.push_back(spanOf(c));
      }
      return;
    }
    const std::string_view text = stripDelimiters(nodeText(node, src));
    const auto start = static_cast<uint32_t>(text.data() - src.data());
    spans.push_back({start, start + static_cast<uint32_t>(text.size())});
  }

  // Escape nodes of a string node's subtree, in source order.
  void escapeSpans(const TSNode node, const std::string_view src, std::vector<TSNode> &stack, std::vector<Span> &escapes) {
    escapes.clear();
    stack.assign(1, node);
    while (!stack.empty()) {
      const TSNode n = stack.back();
      stack.pop_back();
      if (isEscape(n, src)) {
        escapes.push_back(spanOf(n));
        continue;
      }
      for (uint32_t i = ts_node_child_count(n); i-- > 0;)
        stack.push_back(ts_node_child(n, i));
    }
  }

  bool isHex(const char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

  // Length of the escape that starts with the backslash at text[i], for grammars
  // that keep escapes inside a text token. 0 means the backslash is literal.
  // Lexer context: the delimiters a single-quoted Ruby or Perl form escapes (empty for
  // a Perl double-quoted form), and the # count of a Swift extended-delimiter string.
  struct LexContext {
    std::string_view delimiters;
    size_t hashes = 0;
  };

  size_t escapeLength(const EscapeSource source, const std::string_view text, const size_t i, const LexContext &context) {
    const size_t n = text.size();
    size_t j = i + 1;
    switch (source) {
      case EscapeSource::Ruby: // '...' and %q(...): only \\ and an escaped delimiter
        return j < n && (text[j] == '\\' || context.delimiters.find(text[j]) != std::string_view::npos) ? 2 : 0;
      case EscapeSource::Perl: {
        if (!context.delimiters.empty()) // '...' and q(...): only \\ and an escaped delimiter
          return j < n && (text[j] == '\\' || context.delimiters.find(text[j]) != std::string_view::npos) ? 2 : 0;
        if (j >= n)
          return 1;
        const char c = text[j];
        if ((c == 'x' || c == 'o' || c == 'N') && j + 1 < n && text[j + 1] == '{') {
          const size_t close = text.find('}', j);
          return close == std::string_view::npos ? 2 : close + 1 - i;
        }
        size_t k = j + 1;
        if (c == 'x') {
          while (k < n && k < j + 3 && isHex(text[k]))
            ++k;
          return k - i;
        }
        if (c >= '0' && c <= '7') {
          while (k < n && k < j + 3 && text[k] >= '0' && text[k] <= '7')
            ++k;
          return k - i;
        }
        if (c == 'c')
          return j + 1 < n ? 3 : 2;
        return 2;
      }
      case EscapeSource::Swift: // extended delimiters: \#n is an escape in #"..."#
        for (size_t k = 0; k < context.hashes; ++k, ++j)
          if (j >= n || text[j] != '#')
            return 0;
        if (j >= n || text[j] == '(')
          return 0; // interpolation, not an escape
        if (text[j] == 'u' && j + 1 < n && text[j + 1] == '{') {
          const size_t close = text.find('}', j);
          return close == std::string_view::npos ? 0 : close + 1 - i;
        }
        return j + 1 - i;
      case EscapeSource::Kotlin:
        if (j + 4 < n && text[j] == 'u' && isHex(text[j + 1]) && isHex(text[j + 2]) && isHex(text[j + 3]) && isHex(text[j + 4]))
          return 6;
        return j < n ? 2 : 1;
      case EscapeSource::Erlang:
        if (j >= n)
          return 1;
        if (text[j] == '^')
          return j + 1 < n ? 3 : 2;
        if (text[j] == 'x') {
          if (j + 1 < n && text[j + 1] == '{') {
            const size_t close = text.find('}', j);
            return close == std::string_view::npos ? 2 : close + 1 - i;
          }
          size_t k = j + 1;
          while (k < n && k < j + 3 && isHex(text[k]))
            ++k;
          return k - i;
        }
        if (text[j] >= '0' && text[j] <= '7') {
          size_t k = j;
          while (k < n && k < j + 3 && text[k] >= '0' && text[k] <= '7')
            ++k;
          return k - i;
        }
        return 2;
      case EscapeSource::Haskell: {
        if (j >= n)
          return 1;
        const char c = text[j];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { // string gap, up to the closing backslash
          const size_t close = text.find('\\', j);
          return close == std::string_view::npos ? n - i : close + 1 - i;
        }
        if (c == '^')
          return j + 1 < n ? 3 : 2;
        size_t k = j;
        if (c >= '0' && c <= '9') {
          while (k < n && text[k] >= '0' && text[k] <= '9')
            ++k;
          return k - i;
        }
        if ((c == 'o' || c == 'x') && j + 1 < n) {
          k = j + 1;
          while (k < n && (c == 'x' ? isHex(text[k]) : text[k] >= '0' && text[k] <= '7'))
            ++k;
          return k > j + 1 ? k - i : 2;
        }
        // ASCII control names, longest match first (SOH before SO).
        for (const size_t len: {size_t{3}, size_t{2}})
          if (j + len <= n)
            for (const std::string_view name: {"NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS", "HT", "LF",
                                               "VT", "FF", "CR", "SO", "SI", "DLE", "DC1", "DC2", "DC3", "DC4", "NAK",
                                               "SYN", "ETB", "CAN", "EM", "SUB", "ESC", "FS", "GS", "RS", "US", "SP", "DEL"})
              if (name.size() == len && text.substr(j, len) == name)
                return 1 + len;
        return 2;
      }
      case EscapeSource::Nodes:
        break;
    }
    return 0;
  }

  // The delimiters of a quote-like form whose opening delimiter starts `text`
  // (Ruby %q, Perl q): a bracket pair, or the character itself.
  std::string_view quoteDelimiters(const std::string_view text) {
    constexpr std::string_view pairs = "()[]{}<>";
    const size_t p = pairs.find(text[0]);
    return p == std::string_view::npos ? text.substr(0, 1) : pairs.substr(p - p % 2, 2);
  }

  // Splits `text` into literal runs and escapes for grammars without escape nodes.
  void lexEscapes(const EscapeSource source, const std::string_view text, const LexContext &context, RawRewrite::Counter &counter, std::string &escape) {
    size_t literal = 0;
    for (size_t i = 0; i < text.size();) {
      const size_t len = text[i] == '\\' ? escapeLength(source, text, i, context) : 0;
      if (len == 0) {
        ++i;
        continue;
      }
      counter.Literal(text.substr(literal, i - literal));
      if (source == EscapeSource::Swift) { // drop the #s: \#n decodes like \n
        escape.assign(1, '\\');
        escape.append(text.substr(i + 1 + context.hashes, len - 1 - context.hashes));
        counter.Escape(escape);
      } else
        counter.Escape(text.substr(i, len));
      i = literal = i + len;
    }
    counter.Literal(text.substr(literal));
  }

  // Per-walk buffers, reused across string nodes.
  struct StringScratch {
    std::vector<Span> spans;
    std::vector<Span> escapes;
    std::vector<TSNode> stack;
    std::string body;
    std::string escape;
    RawRewrite::Counter counter;
  };

  // Outcome of rewriting one string as a raw literal (see RawRewrite.hpp).
  struct Rewrite {
    uint64_t converted = 0;
    /// The language's default raw form holds the rewritten content verbatim.
    bool rawHolds = false;
    /// A matchertext literal M"(...)" holds the rewritten content verbatim.
    bool mHolds = false;
    /// RawRewrite::AllRawFormViolations and AllRawFormsHolding of the rewritten content.
    uint64_t rawFormViolations = 0;
    uint64_t rawFormsHolding = 0;
    /// One of the language's own raw forms holds the rewritten content.
    bool ownRawForm = false;
    /// The rewritten content holds a control character other than tab, LF, and CR.
    bool control = false;
    /// The converted toothpicks, by cause, and whether the decoded content is not
    /// matchertext, which keeps all of them.
    RawRewrite::Kept kept;
    bool notMatchertext = false;
  };

  // Native raw form outcome of a rewritten content.
  Rewrite rawForms(const LanguageEnum language, Rewrite out, const std::string_view content, const bool encodable) {
    out.rawFormViolations = RawRewrite::AllRawFormViolations(content);
    out.rawFormsHolding = RawRewrite::AllRawFormsHolding(out.rawFormViolations, encodable);
    out.ownRawForm = (out.rawFormsHolding & RawRewrite::OwnRawForms(language)) != 0;
    out.control = RawRewrite::HasControl(content);
    return out;
  }

  // Interpolations a Swift string body opens: \( in a plain string, where \\ is an
  // escaped backslash, and \ then `hashes` #s then ( in an extended-delimiter one.
  uint64_t swiftInterpolations(const std::string_view body, const size_t hashes) {
    uint64_t count = 0;
    for (size_t i = 0; i + 1 < body.size(); ++i) {
      if (body[i] != '\\')
        continue;
      size_t j = i + 1;
      if (hashes == 0 && body[j] != '(') {
        ++i; // an escape: \\, \n, ...
        continue;
      }
      size_t k = 0;
      while (k < hashes && j < body.size() && body[j] == '#')
        ++k, ++j;
      if (k == hashes && j < body.size() && body[j] == '(')
        ++count;
      i = j - 1;
    }
    return count;
  }

  Rewrite rewrite(const LanguageEnum language, const TSNode node, const std::string_view src, const std::string_view body, const EscapeRules &rules, StringScratch &scratch) {
    const std::string_view text = nodeText(node, src);
    const size_t quote = text.find_first_of("\"'`");
    // The opening quote run, for languages whose raw form keeps the quote style.
    std::string_view quoteRun;
    if (quote != std::string_view::npos)
      quoteRun = text.substr(quote, text.substr(quote, 3) == std::string(3, text[quote]) ? 3 : 1);
    if (body.find('\\') == std::string_view::npos)
      return rawForms(language, {0, RawRewrite::DefaultRawHolds(language, body, quoteRun), RawRewrite::MatchertextHolds(body)}, body, true);

    RawRewrite::Counter &counter = scratch.counter;
    counter.Reset(rules.dialect);

    EscapeSource source = rules.source;
    LexContext context;
    if (source == EscapeSource::Ruby) {
      if (text.starts_with('\''))
        context.delimiters = "'";
      else if (text.starts_with("%q") && text.size() > 2)
        context.delimiters = quoteDelimiters(text.substr(2));
      else
        source = EscapeSource::Nodes;
    } else if (source == EscapeSource::Perl && std::string_view(ts_node_type(node)) == "string_literal") {
      // '...' or q(...); double-quoted forms are interpolated_string_literal.
      context.delimiters = text.starts_with('q') && text.size() > 1 ? quoteDelimiters(text.substr(1)) : "'";
    } else if (source == EscapeSource::Swift) {
      context.hashes = text.find_first_not_of('#');
      if (context.hashes == 0 || context.hashes == std::string_view::npos)
        source = EscapeSource::Nodes;
    }
    // Verbatim forms: Kotlin """...""", Erlang triple-quoted strings and sigils, ~B, ~S.
    const bool verbatim =
        (source == EscapeSource::Kotlin && text.starts_with("\"\"\"")) ||
        (source == EscapeSource::Erlang &&
         (text.starts_with("\"\"\"") ||
          (text.starts_with('~') && text.size() > 1 &&
           (std::isupper(static_cast<unsigned char>(text[1])) || text.substr(1).starts_with("\"\"\"")))));

    if (source == EscapeSource::Nodes) {
      escapeSpans(node, src, scratch.stack, scratch.escapes);
      size_t e = 0;
      for (const auto [start, end]: scratch.spans) {
        uint32_t at = start;
        for (; e < scratch.escapes.size() && scratch.escapes[e].start < end; ++e) {
          if (scratch.escapes[e].start < at)
            continue;
          counter.Literal(src.substr(at, scratch.escapes[e].start - at));
          counter.Escape(src.substr(scratch.escapes[e].start, scratch.escapes[e].end - scratch.escapes[e].start));
          at = scratch.escapes[e].end;
        }
        counter.Literal(src.substr(at, end - at));
      }
    } else if (verbatim)
      counter.Literal(body);
    else
      for (const auto [start, end]: scratch.spans)
        lexEscapes(source, src.substr(start, end - start), context, counter, scratch.escape);
    uint64_t converted = counter.Count();
    // Decoding can create matchers (\x5B is '['). A literal whose decoded content
    // is not matchertext cannot be rewritten and keeps all its toothpicks.
    const bool matchertext = RawRewrite::MatchertextHolds(counter.Content());
    if (!matchertext)
      converted = static_cast<uint64_t>(std::ranges::count(body, '\\'));
    Rewrite out = counter.Holdable()
      ? Rewrite{converted, RawRewrite::DefaultRawHolds(language, counter.Content(), quoteRun), matchertext}
      : Rewrite{converted, false, false};
    out.kept = counter.KeptToothpicks();
    out.notMatchertext = !matchertext;
    /// A Swift interpolation \(name), \#(name) in #"..."#, is literal text to the
    /// rewrite; its backslash is not that of an already raw literal.
    if (rules.source == EscapeSource::Swift) {
      const uint64_t opened = swiftInterpolations(body, context.hashes == std::string_view::npos ? 0 : context.hashes);
      const uint64_t moved = std::min(opened, out.kept.raw);
      out.kept.raw -= moved;
      out.kept.interpolation += moved;
    }
    return rawForms(language, out, counter.Content(), counter.Holdable());
  }

  // Body text of the spans: a view into `src` for one span, else concatenated.
  std::string_view bodyText(const std::string_view src, const std::vector<Span> &spans, std::string &out) {
    if (spans.size() == 1)
      return src.substr(spans[0].start, spans[0].end - spans[0].start);
    out.clear();
    for (const auto [start, end]: spans)
      out.append(src.substr(start, end - start));
    return out;
  }

  Serde::JSON makeObj(const char *kind, const std::string_view value) {
    return Serde::JSON::Object({{"kind", Serde::JSON(kind)}, {"value", Serde::JSON(value)}});
  }

  // ---- tree walk -----------------------------------------------------------
  // Iterative (explicit stack) to avoid recursion-depth blowups on deep trees.
  // String/comment nodes are emitted whole and not descended into; ERROR nodes
  // are descended through so well-formed descendants are still collected.
  void walk(const LanguageEnum language, const TSNode root, const std::string_view src, const Classifier &cls, const EscapeRules &rules, Serde::JSON &out) {
    std::vector<TSNode> stack;
    StringScratch scratch; // reused across string nodes
    stack.push_back(root);
    while (!stack.empty()) {
      const TSNode node = stack.back();
      stack.pop_back();

      // Anonymous nodes are tokens such as the TypeScript `string` keyword.
      switch (ts_node_is_named(node) ? cls.classify(ts_node_type(node)) : Kind::Other) {
        case Kind::Comment:
          out.PushBack(makeObj("comment", nodeText(node, src)));
          continue;
        case Kind::String: {
          bodySpans(node, src, scratch.spans);
          const std::string_view body = bodyText(src, scratch.spans, scratch.body);
          const Rewrite r = rewrite(language, node, src, body, rules, scratch);
          out.PushBack(Serde::JSON::Object({
            {"kind", Serde::JSON("string")},
            {"value", Serde::JSON(body)},
            {"convertedToothpicks", Serde::JSON(static_cast<double>(r.converted))},
            {"rawHolds", Serde::JSON(r.rawHolds ? 1.0 : 0.0)},
            {"mHolds", Serde::JSON(r.mHolds ? 1.0 : 0.0)},
            {"rawFormViolations", Serde::JSON(static_cast<double>(r.rawFormViolations))},
            {"rawFormsHolding", Serde::JSON(static_cast<double>(r.rawFormsHolding))},
            {"ownRawForm", Serde::JSON(r.ownRawForm ? 1.0 : 0.0)},
            {"control", Serde::JSON(r.control ? 1.0 : 0.0)},
            {"keptRaw", Serde::JSON(static_cast<double>(r.kept.raw))},
            {"keptUnknownEscape", Serde::JSON(static_cast<double>(r.kept.unknownEscape))},
            {"keptInterpolation", Serde::JSON(static_cast<double>(r.kept.interpolation))},
            {"keptBackslash", Serde::JSON(static_cast<double>(r.kept.backslash))},
            {"keptUnwritable", Serde::JSON(static_cast<double>(r.kept.unwritable))},
            {"keptControl", Serde::JSON(static_cast<double>(r.kept.control))},
            {"notMatchertext", Serde::JSON(r.notMatchertext ? 1.0 : 0.0)},
            {"begin", Serde::JSON(static_cast<double>(ts_node_start_byte(node)))},
            {"end", Serde::JSON(static_cast<double>(ts_node_end_byte(node)))}
          }));
          continue;
        }
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

  // Skip oversized files. Each parse holds the whole file plus its syntax tree
  // (which can be ~10x the source) in memory, and the walk runs under OpenMP
  // across many threads at once, so a handful of huge files parsed concurrently
  // can exhaust RAM and get the process OOM-killed. Files this large are
  // essentially always generated/minified/vendored blobs, not hand-written
  // source, so skipping them costs no real signal.
  constexpr std::streamoff kMaxFileBytes = 16 * 1024 * 1024; // 16 MiB

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

bool TreeSitter::Parse(const LanguageEnum language, const std::string &path, Serde::JSON &result,
                       SkipReason *skip) {
  const auto skipped = [skip](const SkipReason reason) {
    if (skip)
      *skip = reason;
    return false;
  };
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

  if (!result.IsArray())
    result = Serde::JSON::Array();

  // Oversized file: skip before allocating/parsing to bound peak memory.
  if (size > kMaxFileBytes)
    return skipped(SkipReason::Oversized);

  std::string src(static_cast<size_t>(size), '\0');
  in.seekg(0);
  in.read(src.data(), size);
  src.resize(static_cast<size_t>(in.gcount()));

  // Skip binary files. Source extensions collide with non-source formats (e.g.
  // `.ts` is both TypeScript and MPEG transport stream, which Chromium ships as
  // test data). A NUL byte is a reliable binary signal — source essentially
  // never contains one — and feeding binary to the GLR parser triggers
  // pathological error-recovery blowups the parse timeout can't always catch.
  if (std::memchr(src.data(), '\0', src.size()) != nullptr)
    return skipped(SkipReason::Binary);

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
    return skipped(SkipReason::Timeout); // the progress callback cancelled the parse

  walk(language, ts_tree_root_node(tree), src, classifierFor(language), escapeRulesFor(language), result);
  ts_tree_delete(tree);
  return true;
}
