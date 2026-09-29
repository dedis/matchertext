// External scanner for the MinML grammar.
//
// It makes the lexical decisions that depend on what follows, the same way as
// the Go parser in go/markup/minml (see grammar.js).

#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  // The last token was text ending at a space sucker '<' that belongs to the run
  // of name characters after it, so the run at the next token is a name as is.
  bool name_next;
} Scanner;

enum TokenType { TEXT, TAG_NAME, COMMENT_START, RAW_START, REFERENCE, ATTR_NAME };

// MinML whitespace is XML whitespace.
static bool is_space(int32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static bool is_matcher(int32_t c) {
  return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
}

static int32_t closer_of(int32_t c) {
  switch (c) {
  case '(': return ')';
  case '[': return ']';
  case '{': return '}';
  default: return 0;
  }
}

// XML NameStartChar and NameChar ranges, as in go/markup/xml/syntax.go.
static const int32_t name_start_ranges[][2] = {
    {0x003A, 0x003A}, {0x0041, 0x005A}, {0x005F, 0x005F}, {0x0061, 0x007A},
    {0x00C0, 0x00D6}, {0x00D8, 0x00F6}, {0x00F8, 0x02FF}, {0x0370, 0x037D},
    {0x037F, 0x1FFF}, {0x200C, 0x200D}, {0x2070, 0x218F}, {0x2C00, 0x2FEF},
    {0x3001, 0xD7FF}, {0xF900, 0xFDCF}, {0xFDF0, 0xFFFD},
};

static const int32_t name_char_ranges[][2] = {
    {0x002D, 0x002E}, {0x0030, 0x0039}, {0x00B7, 0x00B7}, {0x0300, 0x036F}, {0x203F, 0x2040},
};

static bool in_ranges(int32_t c, const int32_t (*ranges)[2], unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    if (c >= ranges[i][0] && c <= ranges[i][1]) {
      return true;
    }
  }
  return false;
}

static bool is_name_start(int32_t c) {
  return in_ranges(c, name_start_ranges, sizeof name_start_ranges / sizeof name_start_ranges[0]);
}

static bool is_name_char(int32_t c) {
  return is_name_start(c) || in_ranges(c, name_char_ranges, sizeof name_char_ranges / sizeof name_char_ranges[0]);
}

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static bool accept(TSLexer *lexer, enum TokenType type) {
  lexer->mark_end(lexer);
  lexer->result_symbol = type;
  return true;
}

// At '[': a reference is "[name]" with a name of non-space, non-matcher
// characters other than a lone "<" or ">", or an escape such as "[(<)]".
static bool scan_reference(TSLexer *lexer) {
  advance(lexer);
  int32_t first = lexer->lookahead;
  int32_t closer = closer_of(first);
  if (closer != 0) {
    advance(lexer);
    if (lexer->lookahead != '<' && lexer->lookahead != '>') {
      return false;
    }
    advance(lexer);
    if (lexer->lookahead != closer) {
      return false;
    }
    advance(lexer);
    if (lexer->lookahead != ']') {
      return false;
    }
    advance(lexer);
    return accept(lexer, REFERENCE);
  }
  unsigned n = 0;
  while (!lexer->eof(lexer) && !is_space(lexer->lookahead) && !is_matcher(lexer->lookahead)) {
    advance(lexer);
    n++;
  }
  if (lexer->eof(lexer) || lexer->lookahead != ']' || n == 0 || (n == 1 && (first == '<' || first == '>'))) {
    return false;
  }
  advance(lexer);
  return accept(lexer, REFERENCE);
}

// Text up to the next matcher, or up to an element name: a run of non-space,
// non-matcher characters directly before '[' or '{'. A leading '<' in the run
// is a space sucker, so the name starts after it, and a run "<" is no name.
// The names "-" and "+" before '[' open a comment and raw text.
// name_here tells that the run at the token start already had its sucker removed.
static bool scan_markup(Scanner *s, TSLexer *lexer, bool name_here) {
  unsigned pos = 0;     // characters consumed
  int run = -1;         // where the current run of name characters starts
  unsigned run_len = 0;
  int32_t run_first = 0, run_second = 0;
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    if (is_space(c)) {
      run = -1;
    } else if (is_matcher(c)) {
      bool sucker = run_first == '<' && !(name_here && run == 0);
      if ((c == '[' || c == '{') && run >= 0 && !(sucker && run_len == 1)) {
        unsigned name_start = (unsigned)run + (sucker ? 1 : 0);
        if (name_start > 0) {
          // The text before the name; mark_end was set where the name starts.
          s->name_next = sucker;
          lexer->result_symbol = TEXT;
          return true;
        }
        int32_t name_first = sucker ? run_second : run_first;
        unsigned name_len = run_len - (sucker ? 1 : 0);
        if (name_len == 1 && (name_first == '-' || name_first == '+')) {
          if (c != '[') {
            return false;
          }
          advance(lexer);
          return accept(lexer, name_first == '-' ? COMMENT_START : RAW_START);
        }
        return accept(lexer, TAG_NAME);
      }
      break;
    } else {
      if (run < 0) {
        run = (int)pos;
        run_len = 0;
        run_first = c;
        lexer->mark_end(lexer);
      } else if (run_len == 1) {
        run_second = c;
      }
      run_len++;
      advance(lexer);
      pos++;
      if (run_len == 1 && c == '<' && !(name_here && run == 0)) {
        // A name in this run would start after the space sucker.
        lexer->mark_end(lexer);
      }
      continue;
    }
    advance(lexer);
    pos++;
  }
  if (pos == 0) {
    return false;
  }
  return accept(lexer, TEXT);
}

// An XML name directly before '='.
static bool scan_attr_name(TSLexer *lexer) {
  unsigned n = 0;
  bool valid = true;
  while (!lexer->eof(lexer) && lexer->lookahead != '=' && !is_space(lexer->lookahead) &&
         !is_matcher(lexer->lookahead)) {
    int32_t c = lexer->lookahead;
    valid = valid && (n == 0 ? is_name_start(c) : is_name_char(c));
    advance(lexer);
    n++;
  }
  if (lexer->eof(lexer) || lexer->lookahead != '=' || n == 0 || !valid) {
    return false;
  }
  return accept(lexer, ATTR_NAME);
}

void *tree_sitter_minml_external_scanner_create(void) { return ts_calloc(1, sizeof(Scanner)); }

void tree_sitter_minml_external_scanner_destroy(void *payload) { ts_free(payload); }

unsigned tree_sitter_minml_external_scanner_serialize(void *payload, char *buffer) {
  buffer[0] = ((Scanner *)payload)->name_next;
  return 1;
}

void tree_sitter_minml_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  ((Scanner *)payload)->name_next = length > 0 && buffer[0];
}

bool tree_sitter_minml_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *s = payload;
  bool name_here = s->name_next;
  s->name_next = false;
  // Every symbol is valid only during error recovery; leave that to the parser.
  if (valid_symbols[TEXT] && valid_symbols[ATTR_NAME]) {
    return false;
  }
  if (valid_symbols[ATTR_NAME]) {
    return scan_attr_name(lexer);
  }
  if (valid_symbols[REFERENCE] && lexer->lookahead == '[') {
    return scan_reference(lexer);
  }
  if (valid_symbols[TEXT] && valid_symbols[TAG_NAME]) {
    return scan_markup(s, lexer, name_here);
  }
  return false;
}
