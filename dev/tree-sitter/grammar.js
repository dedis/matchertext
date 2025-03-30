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
    _node: $ => choice($.element, $.word, $.text),

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
    // plain_value matches values containing non-identifier chars (e.g. cat.jpg)
    // or starting with a non-letter (e.g. 123). Pure identifiers fall through to
    // $.word so that "http[...]" is still parsed as an element-valued attribute.
    plain_value: $ => token(choice(
      /[a-zA-Z_][a-zA-Z0-9_:-]*[^ \t\n\r\f{}\[\]<>a-zA-Z0-9_:-][^ \t\n\r\f{}\[\]<>]*/,
      /[^ \t\n\r\f{}\[\]<>a-zA-Z_][^ \t\n\r\f{}\[\]<>]*/,
    )),

    text: $ => /[^\[\]{}<>a-zA-Z_]+/,
  }
});
