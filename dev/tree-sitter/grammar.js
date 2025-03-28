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
      field("content", $.content_block),
    )),
    word: $ => /[a-zA-Z][a-zA-Z0-9_:-]*/,
    content_block: $ => seq("[", repeat1($._node), "]"),
    text: $ => /[^\[\]{}<>a-zA-Z]+/,
  }
});
