#ifndef TREE_SITTER_MDSCANNER_UTILS_CPP_
#define TREE_SITTER_MDSCANNER_UTILS_CPP_

#include "block.h"

// Returns true if the block represents a list item
bool is_list_item(Block block) {
  return block >= LIST_ITEM && block <= LIST_ITEM_MAX_INDENTATION;
}

// Returns the indentation level which lines of a list item should have at
// minimum. Should only be called with blocks for which `is_list_item` returns
// true.
uint8_t list_item_indentation(Block block) { return block - LIST_ITEM + 2; }

// Determines if a character is punctuation as defined by the markdown spec.
bool is_punctuation(char c) {
  return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
         (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

// Determines if a character is ascii whitespace as defined by the markdown
// spec.
bool is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

#endif
