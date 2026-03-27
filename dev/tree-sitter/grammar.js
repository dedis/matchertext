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
    // TODO: add the actual grammar rules
    source_file: $ => repeat($._node),
    _node: $ => choice(
      $.element,
      $.text
    ),

    element: $ => seq(
      field("tag", $.tag_name),
      field("content", $.content_block),
    ),
    tag_name: $ => /[a-zA-Z][a-zA-Z0-9_:-]*/,
    content_block: $ => seq("[", repeat($._node), "]"),
    text: $ => token(prec(-1, /[^\[\]{}<>]+/)),

  }
});
