/**
 * @file Minml grammar for tree-sitter
 * @author Philip Hamelink <philip.hamelink@epfl.ch>
 * @license MIT
 *
 * Grammar structure overview:
 *
 * A source file is a flat sequence of _node items. The main node types are:
 *
 *   element                 tag{attrs}[content] — the <> wrappers are optional
 *   content_block           [...] — delimiter-scoped content, used in elements
 *   attr_block              {...} — key=value attribute list
 *   char_ref                [name] / [#123] / [#xABC] — character references
 *   quoted_string           "[...] — verbatim string, brackets not interpreted
 *   raw_block               +[...] — raw literal content (no escaping)
 *   comment                 -[...] — ignored by processors
 *   processing_instruction  ?[...] — out-of-band instructions (like XML PI)
 *   matcher_escape          [[<]], [[>]], etc. — literal bracket characters
 *   text                    plain characters (no brackets, braces, or <>)
 *   word                    identifier-like text (letters, digits, _, :, -)
 *
 * Disambiguation:
 *   - element vs word: both start with [a-zA-Z_]; element has prec(1) and
 *     requires a following content_block, so "tag[...]" is an element while a
 *     bare identifier is a word.
 *   - char_ref vs content_block: char_ref is in _node; content_block is NOT —
 *     it only appears as element.content or attr_value. A bare "[...]" in
 *     content is always a char_ref.
 *   - plain_value vs word in attr_value: plain_value handles unquoted values
 *     with non-identifier characters (e.g. cat.jpg, 123); pure identifiers
 *     fall through to $.word so "http[...]" still parses as an element-valued
 *     attribute.
 *
 * Whitespace:
 *   No global extras — whitespace is either meaningful content (captured by
 *   the text rule inside content_block) or an explicit separator between
 *   attributes in attr_block. This prevents the parser from accepting
 *   syntactically invalid whitespace such as "< tag[...]" or "name =val".
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: "minml",

  extras: $ => [],  // No implicit whitespace; text rule captures it explicitly inside content

  rules: {
    // Root rule. A MinML document is zero or more nodes at the top level.
    // repeat() matches its argument any number of times (including zero).
    source_file: $ => repeat($._node),

    // The set of all things that can appear as content — at the top level,
    // inside a content_block, or anywhere nodes are allowed.
    // The leading underscore makes this a hidden rule: tree-sitter will not
    // create a named _node in the syntax tree; its children are inlined.
    // choice() tries each alternative in order and uses the first that matches.
    _node: $ => choice(
      $.element,              // tag[...] or tag{...}[...]
      $.processing_instruction, // ?[...]
      $.char_ref,             // [name], [#123], [#xAB]
      $.quoted_string,        // "[...]
      $.raw_block,            // +[...]
      $.comment,              // -[...]
      $.matcher_escape,       // [[<]], [[>]], [(<)], [(>)]
      $.word,                 // bare identifier, e.g. "hello"
      $.text,                 // any other characters (punctuation, spaces, digits)
      // Standalone special prefix chars not followed by '[': '-', '+', '?', '"'.
      // When followed by '[' the longer 2-char construct opener wins instead.
      /[-+?"]/,
    ),

    // An element: optionally prefixed with "<" (space-sucker), then a tag name,
    // an optional attribute block, a mandatory content block, and an optional
    // ">" suffix (space-sucker).
    //
    // prec(1, ...) gives this rule higher precedence than the default (0) to
    // resolve the shift/reduce conflict between element and word: when the parser
    // sees a word followed by "[", it prefers element over bare word.
    //
    // seq() matches its arguments in order, left to right.
    // optional() matches zero or one occurrences.
    // field() attaches a named field to a child node so it is accessible by name
    //   in queries and the API (e.g. node.childForFieldName("tag")).
    // alias() renames a node type in the syntax tree — here word becomes tag_name
    //   so consumers can distinguish tag identifiers from content words.
    element: $ => prec(1, seq(
      optional("<"),                                   // space-sucker prefix marker
      field("tag", alias($.word, $.tag_name)),         // the element name
      optional(field("attrs", $.attr_block)),          // optional {name=value ...}
      field("content", $.content_block),               // mandatory [...]
      optional(">"),                                   // space-sucker suffix marker
    )),

    // A bare identifier: starts with a letter or underscore, followed by any
    // number of letters, digits, underscores, colons, or hyphens.
    // Regex: /[a-zA-Z_][a-zA-Z0-9_:-]*/
    //   [a-zA-Z_]      — first char must be a letter or underscore
    //   [a-zA-Z0-9_:-]*— subsequent chars: letters, digits, _, :, or -
    // Colon and hyphen are included to support XML-style namespaced names
    // (e.g. "xml:lang") and hyphenated names (e.g. "data-value").
    word: $ => /[a-zA-Z_][a-zA-Z0-9_:-]*/,

    // A bracketed block of content: "[" followed by zero or more nodes, then "]".
    // This is the body of an element and appears as its "content" field.
    // It is NOT a standalone node (not in _node) — a bare "[...]" at the top
    // level is always a char_ref, never a content_block.
    content_block: $ => seq("[", repeat($._node), "]"),

    // An attribute block: "{" followed by zero or more attributes, then "}".
    //
    // Attributes are separated by whitespace. The explicit optional(/\s+/) before
    // each attribute and at the end handles the inter-attribute spacing, because
    // there is no global extras whitespace rule in this grammar.
    //   optional(/\s+/) — zero or one run of whitespace (space, tab, newline, etc.)
    //                     /\s+/ matches one or more whitespace characters
    //   repeat(seq(...)) — zero or more repetitions of (optional-space + attribute)
    //
    // Example: {src=cat.jpg alt=[a cat]}
    //   → optional whitespace (none before "src")
    //   → attribute(src=cat.jpg)
    //   → optional whitespace (" " before "alt")
    //   → attribute(alt=[a cat])
    //   → optional whitespace (none before "}")
    attr_block: $ => seq(
      "{",
      repeat(seq(optional(/\s+/), $.attribute)),  // whitespace-separated attributes
      optional(/\s+/),                            // optional trailing whitespace before "}"
      "}"
    ),

    // A single attribute: name=value with no whitespace around the "=".
    // The name is an identifier aliased to attr_name in the tree.
    // The value is one of several forms (see attr_value below).
    attribute: $ => seq(
      field("name", alias($.word, $.attr_name)),  // attribute name identifier
      "=",                                        // literal equals sign — no spaces allowed
      field("value", $.attr_value),               // the attribute value
    ),

    // An attribute value is one of four forms, tried in order:
    //   1. prec(1, $.element) — an element like http[//example.com/]
    //                           prec(1) ensures element wins over bare word when
    //                           the value starts with a word followed by "["
    //   2. $.content_block   — a bracketed sequence like [a cute cat]
    //   3. $.plain_value     — an unquoted value with non-identifier characters
    //                           like cat.jpg or 123
    //   4. $.word            — a plain identifier like en or utf-8
    attr_value: $ => choice(
      prec(1, $.element),   // e.g. href=http[//example.com/]
      $.content_block,      // e.g. alt=[a cute cat]
      $.plain_value,        // e.g. src=cat.jpg  or  width=100px
      $.word,               // e.g. lang=en
    ),

    // An unquoted attribute value that contains at least one character outside the
    // identifier set, so it cannot be matched as a plain word.
    //
    // token() wraps the pattern as a single atomic terminal token — the entire
    // match is one leaf node with no internal structure.
    //
    // Two alternatives handle the two cases where an identifier alone is insufficient:
    //
    // Alternative 1: starts with an identifier prefix, then has a non-identifier char
    //   /[a-zA-Z_][a-zA-Z0-9_:-]*/   — leading identifier portion (e.g. "cat" in "cat.jpg")
    //   [^ \t\n\r\f{}\[\]<>a-zA-Z0-9_:-]  — one non-identifier, non-whitespace,
    //                                         non-delimiter char (e.g. ".")
    //   [^ \t\n\r\f{}\[\]<>]*/            — zero or more non-whitespace,
    //                                         non-delimiter chars (e.g. "jpg")
    //   Example matches: cat.jpg  version=1.0  data:image/png
    //   Does NOT match bare identifiers like "en" — those fall through to $.word,
    //   preserving the ability to parse "http[...]" as an element-valued attr.
    //
    // Alternative 2: starts with a non-identifier, non-delimiter character
    //   /[^ \t\n\r\f{}\[\]<>a-zA-Z_]/   — first char is not a letter/underscore/whitespace/delimiter
    //   [^ \t\n\r\f{}\[\]<>]*/           — zero or more non-whitespace, non-delimiter chars
    //   Example matches: 123  42px  @charset
    //
    // Shared exclusion set for "non-delimiter" chars: space, tab, newline (\n),
    // carriage return (\r), form feed (\f), braces {}, square brackets \[\],
    // and angle brackets <>.
    plain_value: $ => token(choice(
      /[a-zA-Z_][a-zA-Z0-9_:-]*[^ \t\n\r\f{}\[\]<>a-zA-Z0-9_:-][^ \t\n\r\f{}\[\]<>]*/,
      /[^ \t\n\r\f{}\[\]<>a-zA-Z_][^ \t\n\r\f{}\[\]<>]*/,
    )),

    // A character reference — the MinML equivalent of &name; / &#123; / &#xAB;
    // Three forms, each wrapped in "[" ... "]":
    //
    //   named_ref:   /[a-zA-Z][a-zA-Z0-9]*/
    //                starts with a letter, followed by letters/digits
    //                Example: [reg]  [amp]  [nbsp]
    //
    //   decimal_ref: /#[0-9]+/
    //                a "#" followed by one or more decimal digits
    //                Example: [#174]  [#169]
    //
    //   hex_ref:     /#x[0-9a-fA-F]+/
    //                "#x" followed by one or more hex digits (upper or lower case)
    //                Example: [#xAE]  [#x00AE]
    //
    // alias() renames each regex match to its descriptive node type in the tree.
    char_ref: $ => seq("[", choice(
      alias(/[a-zA-Z][a-zA-Z0-9]*/,    $.named_ref),    // [reg]
      alias(/#[0-9]+/,                  $.decimal_ref),  // [#174]
      alias(/#x[0-9a-fA-F]+/,          $.hex_ref),      // [#xAE]
    ), "]"),

    // Content rule for verbatim constructs: matches any character sequence that
    // may contain balanced bracket pairs but does NOT interpret MinML inside them.
    // This handles the case where ']' appears inside a quoted string, raw block,
    // comment, or processing instruction — as long as brackets are balanced, the
    // construct is not prematurely closed.
    //
    // Two alternatives, repeated one or more times:
    //   /[^\[\]]+/               — one or more chars that are not '[' or ']'
    //   seq("[", optional(...), "]") — a balanced inner bracket pair
    //
    // Used with optional() in the four constructs below to also allow empty content.
    _raw_content: $ => repeat1(choice(
      /[^\[\]]+/,
      seq("[", optional($._raw_content), "]"),
    )),

    // A quoted string: the two-character delimiter '"[' followed by verbatim
    // content (brackets allowed if balanced), then ']'.
    // Example: "[hello [world]]  — content is "hello [world]"
    quoted_string: $ => seq('"[', optional($._raw_content), "]"),

    // A raw block: '+[' followed by verbatim content (balanced brackets ok), then ']'.
    // Useful for embedding raw markup like HTML.
    // Example: +[<b>bold</b> text]
    raw_block: $ => seq("+[", optional($._raw_content), "]"),

    // A comment: '-[' followed by verbatim content (balanced brackets ok), then ']'.
    // Comments are preserved in the parse tree but ignored by processors.
    // Example: -[this is a [nested] comment]
    comment: $ => seq("-[", optional($._raw_content), "]"),

    // Matcher escape sequences — the MinML way to include literal bracket
    // characters that would otherwise be parsed as structural delimiters.
    //
    // token() makes each a single atomic terminal (no sub-nodes).
    // The four escape forms use square-bracket "matcher" notation:
    //   [[<]]  — literal open square bracket  '['
    //   [[>]]  — literal close square bracket ']'
    //   [(<)]  — literal open parenthesis     '('  (via paren matcher)
    //   [(>)]  — literal close parenthesis    ')'  (via paren matcher)
    matcher_escape: $ => token(choice(
      "[[<]]",   // escaped '['
      "[[>]]",   // escaped ']'
      "[(<)]",   // escaped '('
      "[(>)]",   // escaped ')'
    )),

    // A processing instruction: '?[' followed by verbatim content (balanced brackets
    // ok), then ']'. Used for out-of-band processor directives, similar to XML's <?...?>.
    // Example: ?[xml version="1.0"]
    processing_instruction: $ => seq("?[", optional($._raw_content), "]"),

    // Plain text: any run of characters that are NOT structural delimiters.
    // This is the catch-all rule that captures everything not matched above,
    // including spaces, punctuation, digits, angle brackets, and other characters.
    //
    // /[^\[\]{}a-zA-Z_]+/
    //   [^...]   — negated character class: match any character NOT listed
    //   \[       — exclude '[' (starts char_ref, content_block, etc.)
    //   \]       — exclude ']' (closes content_block / char_ref / etc.)
    //   {        — exclude '{' (starts attr_block)
    //   }        — exclude '}' (closes attr_block)
    //   a-zA-Z_  — exclude letters and underscore (handled by word / element)
    //   +        — one or more characters (text is never empty)
    //
    // Note: '<' and '>' are intentionally included — literal angle brackets in
    // content like p[2 < 3] are valid. The element rule's prec(1) ensures that
    // <tag[...]> sequences are still parsed as space-sucker elements.
    // '-', '+', '?', '"' are excluded because they are the leading characters of
    // special constructs (-[...], +[...], ?[...], "[...]).  When tree-sitter sees
    // one of those chars followed by '[', it picks the longer literal token (the
    // 2-char construct opener) over the 1-char anonymous alternative defined in
    // _node.  When they appear without '[' (e.g. a dash in "a - b"), they are
    // captured by the anonymous /[-+?"]/ alternative in _node.
    text: $ => /[^\[\]{}a-zA-Z_+\-?"]+/,
  }
});
