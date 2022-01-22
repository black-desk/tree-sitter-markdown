#ifndef TREE_SITTER_MDSCANNER_SCANNER_H_
#define TREE_SITTER_MDSCANNER_SCANNER_H_

#include <vector>

#include "../tree_sitter/parser.h"
#include "block.h"

struct Scanner {
  // A stack of open blocks in the current parse state
  std::vector<Block> open_blocks;
  // Parser state flags
  uint16_t state;
  // Number of blocks that have been matched so far. Only changes during
  // matching and is reset after every line ending
  uint8_t matched;
  // Consumed but "unused" indentation. Sometimes a tab needs to be "split" to
  // be used in multiple tokens.
  uint8_t indentation;
  // The current column. Used to decide how many spaces a tab should equal
  uint8_t column;
  // The number of backspaces of the last opening fenced code block or code span
  // delimiter
  uint8_t code_span_delimiter_length;
  // The number of characters remaining in the currrent emphasis delimiter run.
  uint8_t num_emphasis_delimiters_left;

  Scanner();

  // Write the whole state of a Scanner to a byte buffer
  unsigned serialize(char *buffer);

  // Read the whole state of a Scanner from a byte buffer
  // `serizalize` and `deserialize` should be fully symmetric.
  void deserialize(const char *buffer, unsigned length);

  // Advance the lexer one character
  // Also keeps track of the current column, counting tabs as spaces with tab
  // stop 4 See https://github.github.com/gfm/#tabs
  size_t advance(TSLexer *lexer);

  // Convenience function to emit the error token. This is done to stop invalid
  // parse branches. Specifically:
  // 1. When encountering a newline after a line break that ended a paragraph,
  // and no new block has been opened.
  // 2. When encountering a new block after a soft line break.
  // 3. When a `$._trigger_error` token is valid, which is used to stop parse
  // branches through normal tree-sitter grammar rules.
  //
  // See also the `$._soft_line_break` and `$._paragraph_end_newline` tokens in
  // grammar.js
  bool error(TSLexer *lexer);

  bool scan(TSLexer *lexer, const bool *valid_symbols);

  // Try to match the given block, i.e. consume all tokens that belong to the
  // block. These are
  // 1. indentation for list items and indented code blocks
  // 2. '>' for block quotes
  // Returns true if the block is matched and false otherwise
  bool match(TSLexer *lexer, Block block);

  bool parse_backtick(TSLexer *lexer, const bool *valid_symbols);

  bool parse_star(TSLexer *lexer, const bool *valid_symbols);

  bool parse_underscore(TSLexer *lexer, const bool *valid_symbols);
};

#endif
