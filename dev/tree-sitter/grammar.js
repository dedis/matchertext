/**
 * @file Minml grammar for tree-sitter
 * @author Philip Hamelink <philip.hamelink@epfl.ch>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: "minml",

  rules: {
    // TODO: add the actual grammar rules
    source_file: $ => "hello"
  }
});
