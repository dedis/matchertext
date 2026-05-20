# Builds the tree-sitter runtime + one isolated static library per grammar, then
# aggregates them into the `tree-sitter-grammars` INTERFACE target that the main
# binary links. Each grammar bundles its OWN src/tree_sitter/parser.h, so every
# grammar is compiled with ONLY its own src/ on the include path to avoid header
# clashes between grammars and with the core runtime.

set(TS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

if (NOT EXISTS ${TS_ROOT}/tree-sitter/lib/src/lib.c)
  message(FATAL_ERROR
    "tree-sitter submodules not initialized.\n"
    "Run: git submodule update --init --recursive")
endif ()

# --- core runtime -----------------------------------------------------------
add_library(tree-sitter-core STATIC ${TS_ROOT}/tree-sitter/lib/src/lib.c)
target_include_directories(tree-sitter-core
    PUBLIC  ${TS_ROOT}/tree-sitter/lib/include   # tree_sitter/api.h for our C++ code
    PRIVATE ${TS_ROOT}/tree-sitter/lib/src)      # lib.c #includes the rest internally
target_compile_options(tree-sitter-core PRIVATE -w -O2)
set_target_properties(tree-sitter-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- one static lib per grammar ---------------------------------------------
# `name` becomes target ts_<name>; the language symbol tree_sitter_<name> lives
# in the grammar's parser.c. `src_dir` is relative to TS_ROOT and holds parser.c
# (+ optional scanner.c/.cc). Generated tables are huge: -O2 (not -O3) and -w.
function(add_ts_grammar name src_dir)
  set(dir ${TS_ROOT}/${src_dir})
  if (NOT EXISTS ${dir}/parser.c)
    message(FATAL_ERROR "tree-sitter grammar '${name}' missing parser.c at ${dir}")
  endif ()
  set(srcs ${dir}/parser.c)
  if (EXISTS ${dir}/scanner.c)
    list(APPEND srcs ${dir}/scanner.c)
  endif ()
  if (EXISTS ${dir}/scanner.cc)
    list(APPEND srcs ${dir}/scanner.cc)
  endif ()
  add_library(ts_${name} STATIC ${srcs})
  target_include_directories(ts_${name} PRIVATE ${dir})
  target_compile_options(ts_${name} PRIVATE -w -O2)
  set_target_properties(ts_${name} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

add_ts_grammar(javascript tree-sitter-javascript/src)
add_ts_grammar(typescript tree-sitter-typescript/typescript/src)
add_ts_grammar(java       tree-sitter-java/src)
add_ts_grammar(c_sharp    tree-sitter-c-sharp/src)
add_ts_grammar(rust       tree-sitter-rust/src)
add_ts_grammar(ruby       tree-sitter-ruby/src)
add_ts_grammar(php        tree-sitter-php/php/src)
add_ts_grammar(perl       tree-sitter-perl/src)
add_ts_grammar(lua        tree-sitter-lua/src)
add_ts_grammar(swift      tree-sitter-swift/src)
add_ts_grammar(kotlin     tree-sitter-kotlin/src)
add_ts_grammar(r          tree-sitter-r/src)
add_ts_grammar(scala      tree-sitter-scala/src)
add_ts_grammar(haskell    tree-sitter-haskell/src)
add_ts_grammar(ocaml      tree-sitter-ocaml/grammars/ocaml/src)
add_ts_grammar(erlang     tree-sitter-erlang/src)
add_ts_grammar(elixir     tree-sitter-elixir/src)
add_ts_grammar(dart       tree-sitter-dart/src)
add_ts_grammar(objc       tree-sitter-objc/src)
add_ts_grammar(glsl       tree-sitter-glsl/src)
add_ts_grammar(hlsl       tree-sitter-hlsl/src)
add_ts_grammar(go         tree-sitter-go/src)
add_ts_grammar(python     tree-sitter-python/src)

# --- aggregate --------------------------------------------------------------
add_library(tree-sitter-grammars INTERFACE)
target_include_directories(tree-sitter-grammars INTERFACE ${TS_ROOT}/tree-sitter/lib/include)
target_link_libraries(tree-sitter-grammars INTERFACE
    tree-sitter-core
    ts_javascript ts_typescript ts_java ts_c_sharp ts_rust ts_ruby ts_php
    ts_perl ts_lua ts_swift ts_kotlin ts_r ts_scala ts_haskell ts_ocaml
    ts_erlang ts_elixir ts_dart ts_objc ts_glsl ts_hlsl ts_go ts_python)
