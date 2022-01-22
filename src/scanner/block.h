#ifndef TREE_SITTER_MDSCANNER_BLOCK_H_
#define TREE_SITTER_MDSCANNER_BLOCK_H_

#include <stdint.h>

// Description of a block on the block stack.
//
// LIST_ITEM is a list item with minimal indentation (content begins at indent
// level 2) while LIST_ITEM_MAX_INDENTATION represents a list item with maximal
// indentation without being considered a indented code block.
//
// ANONYMOUS represents any block that whose close is not handled by the
// external scanner. (Usually opened using OPEN_BLOCK and closed using
// CLOSE_BLOCK).
enum Block : uint8_t {
  BLOCK_QUOTE,
  INDENTED_CODE_BLOCK,
  LIST_ITEM = 2,
  LIST_ITEM_MAX_INDENTATION = 17,
  FENCED_CODE_BLOCK,
  ANONYMOUS
};

#endif
