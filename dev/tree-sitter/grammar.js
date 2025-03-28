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
    _node: $ => $.text,
    text: $ => /[^\[\]{}<>]+/,
  }
});
