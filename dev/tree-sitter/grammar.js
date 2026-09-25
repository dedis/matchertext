/**
 * @file MinML grammar for tree-sitter
 * @author Philip Hamelink <philip.hamelink@epfl.ch>
 * @license MIT
 *
 * This grammar accepts exactly the documents the Go parser in go/markup/minml
 * accepts, and gives elements, attributes, references, comments, and raw text
 * the same source ranges. bindings/go/minml_test.go checks this against the Go
 * parser on a corpus and on random documents; keep both in step.
 *
 * The decisions that depend on context are made by src/scanner.c:
 *   - a run of non-space, non-matcher characters directly before '[' or '{' is
 *     an element name; a leading '<' is a space sucker, not part of the name;
 *     the names "-" and "+" open a comment and raw text instead;
 *   - "[...]" is a character reference when its content is not empty, is not
 *     "<" or ">", and has no whitespace and no matchers, or is one of the
 *     escapes "[(<)]", "[[>]]", ...;
 *   - an attribute name is an XML name followed by '='.
 *
 * Matchers ( ) [ ] { } must balance everywhere, as in all matchertext.
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: "minml",

  // Whitespace is content, so nothing is skipped.
  extras: (_) => [],

  externals: ($) => [
    $._text,
    $.tag_name,
    $._comment_start,
    $._raw_start,
    $.reference,
    $.attr_name,
  ],

  rules: {
    source_file: ($) => repeat($._markup),

    // Text, elements, and matcher pairs, as at top level and in element content.
    _markup: ($) =>
      choice(
        $._text,
        $.element,
        $.comment,
        $.raw,
        $.reference,
        $.literal,
      ),

    element: ($) =>
      seq(
        field("name", $.tag_name),
        optional(field("attributes", $.attributes)),
        field("content", $.content),
      ),

    content: ($) => seq("[", repeat($._markup), "]"),

    // A matcher pair that is neither element content nor a reference.
    literal: ($) =>
      choice(
        seq("(", repeat($._markup), ")"),
        seq("[", repeat($._markup), "]"),
        seq("{", repeat($._markup), "}"),
      ),

    // Attributes are separated by whitespace.
    attributes: ($) =>
      seq(
        "{",
        optional($._space),
        optional(
          seq(
            $.attribute,
            repeat(seq($._space, $.attribute)),
            optional($._space),
          ),
        ),
        "}",
      ),

    attribute: ($) =>
      seq(
        field("name", $.attr_name),
        "=",
        optional(field("value", choice($.value, $.quoted_value))),
      ),

    // A bracketed value holds text, references, and pairs, but no elements.
    quoted_value: ($) => seq("[", repeat($._value_markup), "]"),

    // An unbracketed value ends at whitespace outside matcher pairs.
    value: ($) =>
      seq(
        choice($._word, $._value_pair_unbracketed),
        repeat(choice($._word, $.reference, $._value_pair)),
      ),

    _value_markup: ($) => choice($._plain, $.reference, $._value_pair),

    _value_pair: ($) =>
      choice($._value_pair_unbracketed, seq("[", repeat($._value_markup), "]")),

    _value_pair_unbracketed: ($) =>
      choice(
        seq("(", repeat($._value_markup), ")"),
        seq("{", repeat($._value_markup), "}"),
      ),

    // Content that MinML does not parse: only matchers must balance.
    comment: ($) => seq($._comment_start, repeat($._verbatim), "]"),

    raw: ($) => seq($._raw_start, repeat($._verbatim), "]"),

    _verbatim: ($) =>
      choice(
        $._plain,
        seq("(", repeat($._verbatim), ")"),
        seq("[", repeat($._verbatim), "]"),
        seq("{", repeat($._verbatim), "}"),
      ),

    _plain: (_) => /[^()\[\]{}]+/,

    _word: (_) => /[^ \t\r\n()\[\]{}]+/,

    _space: (_) => /[ \t\r\n]+/,
  },
});
