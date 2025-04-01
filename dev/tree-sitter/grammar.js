/**
 * @file Minml grammar for tree-sitter
 * @author Philip Hamelink <philip.hamelink@epfl.ch>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: "minml",
  extras: $ => [/\s/],

  rules: {
    source_file: $ => repeat($._node),
    _node: $ => choice($.element, $.char_ref, $.word, $.text),

    element: $ => prec(1, seq(
      field("tag", alias($.word, $.tag_name)),
      optional(field("attrs", $.attr_block)),
      field("content", $.content_block),
    )),
    word: $ => /[a-zA-Z_][a-zA-Z0-9_:-]*/,
    content_block: $ => seq("[", repeat($._node), "]"),

    attr_block: $ => seq("{", repeat($.attribute), "}"),
    attribute: $ => seq(
      field("name", alias($.word, $.attr_name)),
      "=",
      field("value", $.attr_value),
    ),
    attr_value: $ => choice(
      prec(1, $.element),
      $.content_block,
      $.plain_value,
      $.word,
    ),
    plain_value: $ => token(choice(
      /[a-zA-Z_][a-zA-Z0-9_:-]*[^ \t\n\r\f{}\[\]<>a-zA-Z0-9_:-][^ \t\n\r\f{}\[\]<>]*/,
      /[^ \t\n\r\f{}\[\]<>a-zA-Z_][^ \t\n\r\f{}\[\]<>]*/,
    )),

    char_ref: $ => seq("[", choice(
      alias(/[a-zA-Z][a-zA-Z0-9]*/, $.named_ref),
      alias(/#[0-9]+/, $.decimal_ref),
      alias(/#x[0-9a-fA-F]+/, $.hex_ref),
    ), "]"),

    text: $ => /[^\[\]{}<>a-zA-Z_]+/,
  }
});
