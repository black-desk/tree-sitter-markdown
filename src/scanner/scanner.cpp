#include "scanner.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

#include "../tree_sitter/parser.h"
#include "block.h"
#include "scan.cpp"
#include "state.h"
#include "token.h"
#include "utils.cpp"

Scanner::Scanner() {
  assert(sizeof(Block) == sizeof(char));
  assert(ATX_H6_MARKER == ATX_H1_MARKER + 5);
  deserialize(NULL, 0);
}

unsigned Scanner::serialize(char *buffer) {
  size_t i = 0;
  buffer[i++] = state;
  memcpy(&buffer[i], &state, sizeof(uint16_t));
  i += sizeof(uint16_t);
  buffer[i++] = matched;
  buffer[i++] = indentation;
  buffer[i++] = column;
  buffer[i++] = code_span_delimiter_length;
  buffer[i++] = num_emphasis_delimiters_left;
  size_t blocks_count = open_blocks.size();
  if (blocks_count > UINT8_MAX - i) blocks_count = UINT8_MAX - i;
  if (blocks_count > 0) {
    memcpy(&buffer[i], open_blocks.data(), blocks_count);
    i += blocks_count;
  }
  return i;
}

void Scanner::deserialize(const char *buffer, unsigned length) {
  open_blocks.clear();
  state = 0;
  matched = 0;
  indentation = 0;
  column = 0;
  code_span_delimiter_length = 0;
  num_emphasis_delimiters_left = 0;
  if (length > 0) {
    size_t i = 0;
    state = buffer[i++];
    memcpy(&state, &buffer[i], sizeof(uint16_t));
    i += sizeof(uint16_t);
    matched = buffer[i++];
    indentation = buffer[i++];
    column = buffer[i++];
    code_span_delimiter_length = buffer[i++];
    num_emphasis_delimiters_left = buffer[i++];
    size_t blocks_count = length - i;
    open_blocks.resize(blocks_count);
    if (blocks_count > 0) {
      memcpy(open_blocks.data(), &buffer[i], blocks_count);
    }
  }
}

size_t Scanner::advance(TSLexer *lexer) {
  size_t size = 1;
  if (lexer->lookahead == '\t') {
    size = (column % 4 == 0) ? 4 : (4 - column % 4);
  }
  column += size;
  lexer->advance(lexer, false);
  return size;
}

bool Scanner::error(TSLexer *lexer) {
  lexer->result_symbol = ERROR;
  return true;
}

// Try to match the given block, i.e. consume all tokens that belong to the
// block. These are
// 1. indentation for list items and indented code blocks
// 2. '>' for block quotes
// Returns true if the block is matched and false otherwise
bool Scanner::match(TSLexer *lexer, Block block) {
  switch (block) {
    case INDENTED_CODE_BLOCK:
      if (indentation >= 4 && lexer->lookahead != '\n' &&
          lexer->lookahead != '\r') {
        indentation -= 4;
        return true;
      }
      break;
    case LIST_ITEM:
    case LIST_ITEM + 1:
    case LIST_ITEM + 2:
    case LIST_ITEM + 3:
    case LIST_ITEM + 4:
    case LIST_ITEM + 5:
    case LIST_ITEM + 6:
    case LIST_ITEM + 7:
    case LIST_ITEM + 8:
    case LIST_ITEM + 9:
    case LIST_ITEM + 10:
    case LIST_ITEM + 11:
    case LIST_ITEM + 12:
    case LIST_ITEM + 13:
    case LIST_ITEM + 14:
    case LIST_ITEM + 15:
      if (indentation >= list_item_indentation(open_blocks[matched])) {
        indentation -= list_item_indentation(open_blocks[matched]);
        return true;
      }
      if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        indentation = 0;
        return true;
      }
      break;
    case BLOCK_QUOTE:
      if (lexer->lookahead == '>') {
        advance(lexer);
        indentation = 0;
        if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
          indentation += advance(lexer) - 1;
        }
        return true;
      }
      break;
    case FENCED_CODE_BLOCK:
    case ANONYMOUS:
      return true;
  }
  return false;
}

bool Scanner::parse_backtick(TSLexer *lexer, const bool *valid_symbols) {
  // count the number of backticks
  size_t level = 0;
  while (lexer->lookahead == '`') {
    advance(lexer);
    level++;
  }
  lexer->mark_end(lexer);
  // If this is able to close a fenced code block then that is the only valid
  // interpretation. It can only close a fenced code block if the number of
  // backticks is at least the number of backticks of the opening delimiter.
  // Also it cannot be indented more than 3 spaces.
  if (valid_symbols[FENCED_CODE_BLOCK_END_BACKTICK] && indentation < 4 &&
      level >= code_span_delimiter_length &&
      (lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
    lexer->result_symbol = FENCED_CODE_BLOCK_END_BACKTICK;
    return true;
  }
  // If this could be the start of a fenced code block, check if the info
  // string contains any backticks.
  if (valid_symbols[FENCED_CODE_BLOCK] && level >= 3) {
    bool info_string_has_backtick = false;
    while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
           !lexer->eof(lexer)) {
      if (lexer->lookahead == '`') {
        info_string_has_backtick = true;
        break;
      }
      advance(lexer);
    }
    // If it does not then choose to interpret this as the start of a fenced
    // code block.
    if (!info_string_has_backtick) {
      lexer->result_symbol = FENCED_CODE_BLOCK_START_BACKTICK;
      if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
      state &= ~STATE_NEED_OPEN_BLOCK;
      open_blocks.push_back(FENCED_CODE_BLOCK);
      // Remember the length of the delimiter for later, since we need it to
      // decide whether a sequence of backticks can close the block.
      code_span_delimiter_length = level;
      indentation = 0;
      return true;
    }
  }
  // Otherwise this could be the opening / closing delimiter of a code span,
  // but only if there is no preceeding whitespace. (`indentation` should best
  // only be used to parse tokens related to block structure)
  if (indentation == 0) {
    // If the sequence is exactly as long as the opening delmiter then we
    // interpret this as a closing delimiter. Otherwise it could be a opening
    // delimiter.
    if (level == code_span_delimiter_length && valid_symbols[CODE_SPAN_CLOSE]) {
      lexer->result_symbol = CODE_SPAN_CLOSE;
      return true;
    } else if (valid_symbols[CODE_SPAN_START]) {
      code_span_delimiter_length = level;
      lexer->result_symbol = CODE_SPAN_START;
      return true;
    }
  }
  return false;
}

bool Scanner::parse_star(TSLexer *lexer, const bool *valid_symbols) {
  // If `num_emphasis_delimiters_left` is not zero then we already decided
  // that this should be part of an emphasis delimiter run, so interpret it as
  // such.
  if (num_emphasis_delimiters_left > 0) {
    // The `STATE_EMPHASIS_DELIMITER_IS_OPEN` state flag tells us wether it
    // should be open or close.
    if ((state & STATE_EMPHASIS_DELIMITER_IS_OPEN) &&
        valid_symbols[EMPHASIS_OPEN_STAR]) {
      advance(lexer);
      lexer->result_symbol = EMPHASIS_OPEN_STAR;
      num_emphasis_delimiters_left--;
      return true;
    } else if (valid_symbols[EMPHASIS_CLOSE_STAR]) {
      advance(lexer);
      lexer->result_symbol = EMPHASIS_CLOSE_STAR;
      num_emphasis_delimiters_left--;
      return true;
    }
  }
  advance(lexer);
  lexer->mark_end(lexer);
  // Otherwise count the number of stars permitting whitespaces between them.
  size_t star_count = 1;
  // Also remember how many stars there are before the first whitespace...
  bool had_whitespace = false;
  size_t star_count_before_whitespace = 1;
  // ...and how many spaces follow the first star.
  size_t extra_indentation = 0;
  for (;;) {
    if (lexer->lookahead == '*') {
      if (star_count == 1 && extra_indentation >= 1 &&
          valid_symbols[LIST_MARKER_STAR]) {
        // If we get to this point then the token has to be at least this
        // long. We need to call `mark_end` here in case we decide later that
        // this is a list item.
        lexer->mark_end(lexer);
      }
      if (!had_whitespace) {
        star_count_before_whitespace++;
      }
      star_count++;
      advance(lexer);
    } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      had_whitespace = true;
      if (star_count == 1) {
        extra_indentation += advance(lexer);
      } else {
        advance(lexer);
      }
    } else {
      break;
    }
  }
  bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r';
  bool dont_interrupt = false;
  if (star_count == 1 && line_end) {
    extra_indentation = 1;
    // line is empty so don't interrupt paragraphs if this is a list marker
    dont_interrupt =
        !(state & STATE_JUST_CLOSED) && matched == open_blocks.size();
  }
  // If there were at least 3 stars then this could be a thematic break
  bool thematic_break = star_count >= 3 && line_end;
  // If there was a star and at least one space after that star then this
  // could be a list marker.
  bool list_marker_star = star_count >= 1 && extra_indentation >= 1;
  if (valid_symbols[THEMATIC_BREAK] && thematic_break && indentation < 4) {
    // If a thematic break is valid then it takes precedence
    if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
    state &= ~STATE_NEED_OPEN_BLOCK;
    lexer->result_symbol = THEMATIC_BREAK;
    lexer->mark_end(lexer);
    indentation = 0;
    return true;
  } else if ((dont_interrupt ? valid_symbols[LIST_MARKER_STAR_DONT_INTERRUPT]
                             : valid_symbols[LIST_MARKER_STAR]) &&
             list_marker_star) {
    // List markers take precedence over emphasis markers
    if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
    if (!dont_interrupt) state &= ~STATE_NEED_OPEN_BLOCK;
    if (dont_interrupt && (state & STATE_NEED_OPEN_BLOCK)) return error(lexer);
    // If star_count > 1 then we already called mark_end at the right point.
    // Otherwise the token should go until this point.
    if (star_count == 1) {
      lexer->mark_end(lexer);
    }
    // Not counting one space...
    extra_indentation--;
    // ... check if the list item begins with an indented code block
    if (extra_indentation <= 3) {
      // If not then calculate the indentation level of the list item content
      // as indentation of list marker + indentation after list marker - 1
      extra_indentation += indentation;
      indentation = 0;
    } else {
      // Otherwise the indentation level is just the indentation of the list
      // marker. We keep the indentation after the list marker for later
      // blocks.
      size_t temp = indentation;
      indentation = extra_indentation;
      extra_indentation = temp;
    }
    open_blocks.push_back(Block(LIST_ITEM + extra_indentation));
    lexer->result_symbol =
        dont_interrupt ? LIST_MARKER_STAR_DONT_INTERRUPT : LIST_MARKER_STAR;
    return true;
  } else if (valid_symbols[EMPHASIS_OPEN_STAR] ||
             valid_symbols[EMPHASIS_CLOSE_STAR]) {
    // Be careful to not inlcude any parsed indentation. The indentation
    // variable is only for block structure.
    if (indentation > 0) return false;
    // The desicion made for the first star also counts for all the following
    // stars in the delimiter run. Rembemer how many there are.
    num_emphasis_delimiters_left = star_count_before_whitespace - 1;
    // Look ahead to the next symbol (after the last star) to find out if it
    // is whitespace punctuation or other.
    bool next_symbol_whitespace = had_whitespace || line_end;
    bool next_symbol_punctuation =
        !had_whitespace && is_punctuation(lexer->lookahead);
    // Information about the last token is in valid_symbols. See grammar.js
    // for these tokens for how this is done.
    if (valid_symbols[EMPHASIS_CLOSE_STAR] &&
        !valid_symbols[LAST_TOKEN_WHITESPACE] &&
        (!valid_symbols[LAST_TOKEN_PUNCTUATION] || next_symbol_punctuation ||
         next_symbol_whitespace)) {
      // Closing delimiters take precedence
      state &= ~STATE_EMPHASIS_DELIMITER_IS_OPEN;
      lexer->result_symbol = EMPHASIS_CLOSE_STAR;
      return true;
    } else if (!next_symbol_whitespace &&
               (!next_symbol_punctuation ||
                valid_symbols[LAST_TOKEN_PUNCTUATION] ||
                valid_symbols[LAST_TOKEN_WHITESPACE])) {
      state |= STATE_EMPHASIS_DELIMITER_IS_OPEN;
      lexer->result_symbol = EMPHASIS_OPEN_STAR;
      return true;
    }
  }
  return false;
}

bool Scanner::parse_underscore(TSLexer *lexer, const bool *valid_symbols) {
  if (num_emphasis_delimiters_left > 0) {
    if ((state & STATE_EMPHASIS_DELIMITER_IS_OPEN) &&
        valid_symbols[EMPHASIS_OPEN_UNDERSCORE]) {
      advance(lexer);
      lexer->result_symbol = EMPHASIS_OPEN_UNDERSCORE;
      num_emphasis_delimiters_left--;
      return true;
    } else if (valid_symbols[EMPHASIS_CLOSE_UNDERSCORE]) {
      advance(lexer);
      lexer->result_symbol = EMPHASIS_CLOSE_UNDERSCORE;
      num_emphasis_delimiters_left--;
      return true;
    }
  }
  advance(lexer);
  lexer->mark_end(lexer);
  size_t underscore_count = 1;
  size_t underscore_count_before_whitespace = 1;
  bool encountered_whitespace = false;
  for (;;) {
    if (lexer->lookahead == '_') {
      underscore_count++;
      if (!encountered_whitespace) underscore_count_before_whitespace++;
      advance(lexer);
    } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      if (!encountered_whitespace) {
        encountered_whitespace = true;
      }
      advance(lexer);
    } else {
      break;
    }
  }
  bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r';
  if (underscore_count >= 3 && line_end && valid_symbols[THEMATIC_BREAK]) {
    if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
    state &= ~STATE_NEED_OPEN_BLOCK;
    lexer->result_symbol = THEMATIC_BREAK;
    lexer->mark_end(lexer);
    indentation = 0;
    return true;
  }
  if (valid_symbols[EMPHASIS_OPEN_UNDERSCORE] ||
      valid_symbols[EMPHASIS_CLOSE_UNDERSCORE]) {
    num_emphasis_delimiters_left = underscore_count_before_whitespace - 1;
    bool next_symbol_whitespace = encountered_whitespace || line_end;
    bool next_symbol_punctuation =
        !encountered_whitespace && is_punctuation(lexer->lookahead);
    bool right_flanking = !valid_symbols[LAST_TOKEN_WHITESPACE] &&
                          (!valid_symbols[LAST_TOKEN_PUNCTUATION] ||
                           next_symbol_punctuation || next_symbol_whitespace);
    bool left_flanking =
        !next_symbol_whitespace &&
        (!next_symbol_punctuation || valid_symbols[LAST_TOKEN_PUNCTUATION] ||
         valid_symbols[LAST_TOKEN_WHITESPACE]);
    if (valid_symbols[EMPHASIS_CLOSE_UNDERSCORE] && right_flanking &&
        (!left_flanking || next_symbol_punctuation)) {
      state &= ~STATE_EMPHASIS_DELIMITER_IS_OPEN;
      lexer->result_symbol = EMPHASIS_CLOSE_UNDERSCORE;
      return true;
    } else if (left_flanking &&
               (!right_flanking || valid_symbols[LAST_TOKEN_PUNCTUATION])) {
      state |= STATE_EMPHASIS_DELIMITER_IS_OPEN;
      lexer->result_symbol = EMPHASIS_OPEN_UNDERSCORE;
      return true;
    }
  }
  return false;
}
