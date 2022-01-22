#include "scanner.h"
#include "token.h"
#include "state.h"

bool Scanner::scan(TSLexer *lexer, const bool *valid_symbols) {
  // A normal tree-sitter rule decided that the current branch is invalid and
  // now "requests" an error to stop the branch
  if (valid_symbols[TRIGGER_ERROR]) {
    return error(lexer);
  }

  // If we already matched all currently open blocks and just parsed a
  // `$._paragraph_end_newline` leave the matching state.
  uint8_t split_token_count = (state & STATE_SPLIT_TOKEN_COUNT) >> 5;
  if (split_token_count == 2 && !valid_symbols[SOFT_LINE_BREAK_MARKER] &&
      matched == open_blocks.size()) {
    state &= ~STATE_MATCHING;
  }

  // The parser just encountered a line break. Setup the state correspondingly
  if (valid_symbols[LINE_ENDING]) {
    // If the last line break ended a paragraph and no new block opened, the
    // last line break should have been a soft line break
    if (state & STATE_NEED_OPEN_BLOCK) return error(lexer);
    // Reset the counter for matched blocks
    matched = 0;
    // If there is at least one open block, we should be in the matching
    // state. Also set the matching flag if a `$._soft_line_break_marker` can
    // be emitted so it does get emitted.
    if (valid_symbols[SOFT_LINE_BREAK_MARKER] || open_blocks.size() > 0) {
      state |= STATE_MATCHING;
    } else {
      state &= (~STATE_MATCHING);
    }
    // reset some state variables
    state &= (~STATE_WAS_SOFT_LINE_BREAK) & (~STATE_SPLIT_TOKEN_COUNT) &
             (~STATE_NEED_OPEN_BLOCK) & (~STATE_JUST_CLOSED);
    indentation = 0;
    column = 0;
    lexer->result_symbol = LINE_ENDING;
    return true;
  }

  // Open a new (anonymous) block as requested. See `$._open_block` in
  // grammar.js
  if (valid_symbols[OPEN_BLOCK] ||
      valid_symbols[OPEN_BLOCK_DONT_INTERRUPT_PARAGRAPH]) {
    if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
    if (valid_symbols[OPEN_BLOCK]) state &= ~STATE_NEED_OPEN_BLOCK;
    open_blocks.push_back(ANONYMOUS);
    lexer->result_symbol = valid_symbols[OPEN_BLOCK]
                               ? OPEN_BLOCK
                               : OPEN_BLOCK_DONT_INTERRUPT_PARAGRAPH;
    return true;
  }

  // Close the inner most block after the next line break as requested. See
  // `$._close_block` in grammar.js
  if (valid_symbols[CLOSE_BLOCK]) {
    state |= STATE_CLOSE_BLOCK;
    lexer->result_symbol = CLOSE_BLOCK;
    return true;
  }

  // Parse any preceeding whitespace and remember its length. This makes a lot
  // of parsing quite a bit easier.
  for (;;) {
    if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      indentation += advance(lexer);
    } else {
      break;
    }
  }

  // if we are at the end of the file and there are still open blocks close
  // them all
  if (lexer->eof(lexer)) {
    if (open_blocks.size() > 0) {
      Block block = open_blocks[open_blocks.size() - 1];
      lexer->result_symbol = BLOCK_CLOSE;
      open_blocks.pop_back();
      return true;
    }
    return false;
  }

  if (!(state & STATE_MATCHING)) {
    // We are not matching. This is where the parsing logic for most "normal"
    // token is. Most importantly parsing logic for the start of new blocks.
    if (valid_symbols[INDENTED_CHUNK_START] &&
        !valid_symbols[NO_INDENTED_CHUNK]) {
      if (indentation >= 4 && lexer->lookahead != '\n' &&
          lexer->lookahead != '\r') {
        lexer->result_symbol = INDENTED_CHUNK_START;
        open_blocks.push_back(INDENTED_CODE_BLOCK);
        indentation -= 4;
        return true;
      }
    }
    // Decide which tokens to consider based on the first non-whitespace
    // character
    switch (lexer->lookahead) {
      case '\r':
      case '\n':
        if (valid_symbols[BLANK_LINE_START]) {
          // A blank line token is actually just 0 width, so do not consume
          // the characters
          if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
          state &= ~STATE_NEED_OPEN_BLOCK;
          lexer->result_symbol = BLANK_LINE_START;
          return true;
        }
        break;
      case '`':
        // A backtick could mark the beginning or ending of a code span or a
        // fenced code block.
        return parse_backtick(lexer, valid_symbols);
        break;
      case '*':
        // A star could either mark the beginning or ending of emphasis, a
        // list item or thematic break. This code is similar to the code for
        // '_' and '+'.
        return parse_star(lexer, valid_symbols);
        break;
      case '_':
        return parse_underscore(lexer, valid_symbols);
        break;
      case '>':
        if (valid_symbols[BLOCK_QUOTE_START]) {
          if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
          state &= ~STATE_NEED_OPEN_BLOCK;
          advance(lexer);
          indentation = 0;
          if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            indentation += advance(lexer) - 1;
          }
          lexer->result_symbol = BLOCK_QUOTE_START;
          open_blocks.push_back(BLOCK_QUOTE);
          return true;
        }
        break;
      case '~':
        if (valid_symbols[FENCED_CODE_BLOCK_START_TILDE] ||
            valid_symbols[FENCED_CODE_BLOCK_END_TILDE]) {
          size_t level = 0;
          while (lexer->lookahead == '~') {
            advance(lexer);
            level++;
          }
          if (valid_symbols[FENCED_CODE_BLOCK_END_TILDE] && indentation < 4 &&
              level >= code_span_delimiter_length &&
              (lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
            lexer->result_symbol = FENCED_CODE_BLOCK_END_TILDE;
            return true;
          }
          if (valid_symbols[FENCED_CODE_BLOCK_START_TILDE] && level >= 3) {
            if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
            state &= ~STATE_NEED_OPEN_BLOCK;
            lexer->result_symbol = FENCED_CODE_BLOCK_START_TILDE;
            open_blocks.push_back(FENCED_CODE_BLOCK);
            code_span_delimiter_length = level;
            indentation = 0;
            return true;
          }
        }
        break;
      case '#':
        if (valid_symbols[ATX_H1_MARKER] && indentation <= 3) {
          lexer->mark_end(lexer);
          size_t level = 0;
          while (lexer->lookahead == '#' && level <= 6) {
            advance(lexer);
            level++;
          }
          if (level <= 6 &&
              (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
               lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
            if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
            state &= ~STATE_NEED_OPEN_BLOCK;
            lexer->result_symbol = ATX_H1_MARKER + (level - 1);
            indentation = 0;
            lexer->mark_end(lexer);
            return true;
          }
        }
        break;
      case '=':
        if (valid_symbols[SETEXT_H1_UNDERLINE] &&
            matched == open_blocks.size()) {
          lexer->mark_end(lexer);
          while (lexer->lookahead == '=') {
            advance(lexer);
          }
          while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            advance(lexer);
          }
          if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
            state &= ~STATE_NEED_OPEN_BLOCK;
            lexer->result_symbol = SETEXT_H1_UNDERLINE;
            lexer->mark_end(lexer);
            return true;
          }
        }
        break;
      case '+':
        if (indentation <= 3 &&
            (valid_symbols[LIST_MARKER_PLUS] ||
             valid_symbols[LIST_MARKER_PLUS_DONT_INTERRUPT])) {
          advance(lexer);
          size_t extra_indentation = 0;
          while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            extra_indentation += advance(lexer);
          }
          bool dont_interrupt = false;
          if (lexer->lookahead == '\r' || lexer->lookahead == '\n') {
            extra_indentation = 1;
            dont_interrupt = true;
          }
          dont_interrupt = dont_interrupt && !(state & STATE_JUST_CLOSED) &&
                           matched == open_blocks.size();
          if (extra_indentation >= 1 &&
              (dont_interrupt ? valid_symbols[LIST_MARKER_PLUS_DONT_INTERRUPT]
                              : valid_symbols[LIST_MARKER_PLUS])) {
            if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
            if (!dont_interrupt) state &= ~STATE_NEED_OPEN_BLOCK;
            if (dont_interrupt && (state & STATE_NEED_OPEN_BLOCK))
              return error(lexer);
            lexer->result_symbol = dont_interrupt
                                       ? LIST_MARKER_PLUS_DONT_INTERRUPT
                                       : LIST_MARKER_PLUS;
            extra_indentation--;
            if (extra_indentation <= 3) {
              extra_indentation += indentation;
              indentation = 0;
            } else {
              size_t temp = indentation;
              indentation = extra_indentation;
              extra_indentation = temp;
            }
            open_blocks.push_back(Block(LIST_ITEM + extra_indentation));
            return true;
          }
        }
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        if (indentation <= 3 && (valid_symbols[LIST_MARKER_PARENTHESIS] ||
                                 valid_symbols[LIST_MARKER_DOT])) {
          size_t digits = 1;
          bool dont_interrupt = lexer->lookahead != '1';
          advance(lexer);
          while (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
            dont_interrupt = true;
            digits++;
            advance(lexer);
          }
          if (digits >= 1 && digits <= 9) {
            bool success = false;
            bool dot = false;
            bool parenthesis = false;
            if (lexer->lookahead == '.') {
              advance(lexer);
              dot = true;
            } else if (lexer->lookahead == ')') {
              advance(lexer);
              parenthesis = true;
            }
            if (dot || parenthesis) {
              size_t extra_indentation = 0;
              while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                extra_indentation += advance(lexer);
              }
              bool line_end =
                  lexer->lookahead == '\n' || lexer->lookahead == '\r';
              if (line_end) {
                extra_indentation = 1;
                dont_interrupt = true;
              }
              dont_interrupt = dont_interrupt && !(state & STATE_JUST_CLOSED) &&
                               matched == open_blocks.size();
              if (extra_indentation >= 1 &&
                  (dot ? (dont_interrupt
                              ? valid_symbols[LIST_MARKER_DOT_DONT_INTERRUPT]
                              : valid_symbols[LIST_MARKER_DOT])
                       : (dont_interrupt
                              ? valid_symbols
                                    [LIST_MARKER_PARENTHESIS_DONT_INTERRUPT]
                              : valid_symbols[LIST_MARKER_PARENTHESIS]))) {
                if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
                if (!dont_interrupt) state &= ~STATE_NEED_OPEN_BLOCK;
                if (dont_interrupt && (state & STATE_NEED_OPEN_BLOCK))
                  return error(lexer);
                lexer->result_symbol =
                    dot ? LIST_MARKER_DOT : LIST_MARKER_PARENTHESIS;
                extra_indentation--;
                if (extra_indentation <= 3) {
                  extra_indentation += indentation;
                  indentation = 0;
                } else {
                  size_t temp = indentation;
                  indentation = extra_indentation;
                  extra_indentation = temp;
                }
                open_blocks.push_back(
                    Block(LIST_ITEM + extra_indentation + digits));
                return true;
              }
            }
          }
        }
        break;
      case '-':
        if (indentation <= 3 &&
            (valid_symbols[LIST_MARKER_MINUS] ||
             valid_symbols[LIST_MARKER_MINUS_DONT_INTERRUPT] ||
             valid_symbols[SETEXT_H2_UNDERLINE] ||
             valid_symbols[THEMATIC_BREAK])) {
          lexer->mark_end(lexer);
          bool whitespace_after_minus = false;
          bool minus_after_whitespace = false;
          size_t minus_count = 0;
          size_t extra_indentation = 0;

          for (;;) {
            if (lexer->lookahead == '-') {
              if (minus_count == 1 && extra_indentation >= 1) {
                lexer->mark_end(lexer);
              }
              minus_count++;
              advance(lexer);
              minus_after_whitespace = whitespace_after_minus;
            } else if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
              if (minus_count == 1) {
                extra_indentation += advance(lexer);
              } else {
                advance(lexer);
              }
              whitespace_after_minus = true;
            } else {
              break;
            }
          }
          bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r';
          bool dont_interrupt = false;
          if (minus_count == 1 && line_end) {
            extra_indentation = 1;
            dont_interrupt = true;
          }
          dont_interrupt = dont_interrupt && !(state & STATE_JUST_CLOSED) &&
                           matched == open_blocks.size();
          bool thematic_break = minus_count >= 3 && line_end;
          bool underline =
              minus_count >= 1 && !minus_after_whitespace && line_end &&
              matched == open_blocks.size();  // setext heading can not break
                                              // lazy continuation
          bool list_marker_minus = minus_count >= 1 && extra_indentation >= 1;
          if (valid_symbols[SETEXT_H2_UNDERLINE] && underline) {
            if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
            state &= ~STATE_NEED_OPEN_BLOCK;
            lexer->result_symbol = SETEXT_H2_UNDERLINE;
            lexer->mark_end(lexer);
            indentation = 0;
            return true;
          } else if (valid_symbols[THEMATIC_BREAK] &&
                     thematic_break) {  // underline is false if
                                        // list_marker_minus is true
            if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
            state &= ~STATE_NEED_OPEN_BLOCK;
            lexer->result_symbol = THEMATIC_BREAK;
            lexer->mark_end(lexer);
            indentation = 0;
            return true;
          } else if ((dont_interrupt
                          ? valid_symbols[LIST_MARKER_MINUS_DONT_INTERRUPT]
                          : valid_symbols[LIST_MARKER_MINUS]) &&
                     list_marker_minus) {
            if (state & STATE_WAS_SOFT_LINE_BREAK) return error(lexer);
            if (!dont_interrupt) state &= ~STATE_NEED_OPEN_BLOCK;
            if (dont_interrupt && (state & STATE_NEED_OPEN_BLOCK))
              return error(lexer);
            if (minus_count == 1) {
              lexer->mark_end(lexer);
            }
            extra_indentation--;
            if (extra_indentation <= 3) {
              extra_indentation += indentation;
              indentation = 0;
            } else {
              size_t temp = indentation;
              indentation = extra_indentation;
              extra_indentation = temp;
            }
            open_blocks.push_back(Block(LIST_ITEM + extra_indentation));
            lexer->result_symbol = dont_interrupt
                                       ? LIST_MARKER_MINUS_DONT_INTERRUPT
                                       : LIST_MARKER_MINUS;
            return true;
          }
        }
        break;
    }
  } else {  // we are in the state of trying to match all currently open
            // blocks
    bool partial_success = false;
    while (matched < open_blocks.size()) {
      if (matched == open_blocks.size() - 1 && (state & STATE_CLOSE_BLOCK)) {
        if (!partial_success) state &= ~STATE_CLOSE_BLOCK;
        break;
      }
      // If next block is a block quote and we have already matched stuff then
      // return as every continuation for block quotes should be its own
      // token.
      if (open_blocks[matched] == BLOCK_QUOTE && partial_success) {
        break;
      }
      if (match(lexer, open_blocks[matched])) {
        partial_success = true;
        matched++;
        // Return after every block quote continuation
        if (open_blocks[matched - 1] == BLOCK_QUOTE) {
          break;
        }
      } else {
        break;
      }
    }
    if (partial_success) {
      if (!valid_symbols[SOFT_LINE_BREAK_MARKER] &&
          matched == open_blocks.size()) {
        state &= (~STATE_MATCHING);
      }
      if (open_blocks[matched - 1] == BLOCK_QUOTE) {
        lexer->result_symbol = BLOCK_QUOTE_CONTINUATION;
      } else {
        lexer->result_symbol = BLOCK_CONTINUATION;
      }
      return true;
    }

    uint8_t split_token_count = (state & STATE_SPLIT_TOKEN_COUNT) >> 5;
    if (valid_symbols[SPLIT_TOKEN] && split_token_count < 2) {
      split_token_count++;
      state &= ~STATE_SPLIT_TOKEN_COUNT;
      state |= split_token_count << 5;
      state |= STATE_NEED_OPEN_BLOCK;
      lexer->result_symbol = SPLIT_TOKEN;
      return true;
    }
    if (!valid_symbols[SOFT_LINE_BREAK_MARKER]) {
      Block block = open_blocks[open_blocks.size() - 1];
      lexer->result_symbol = BLOCK_CLOSE;
      if (block == FENCED_CODE_BLOCK) {
        lexer->mark_end(lexer);
        indentation = 0;
      }
      open_blocks.pop_back();
      if (matched == open_blocks.size()) {
        state &= (~STATE_MATCHING);
      }
      state |= STATE_JUST_CLOSED;
      return true;
    } else {
      state &= (~STATE_MATCHING) & (~STATE_NEED_OPEN_BLOCK);
      state |= STATE_WAS_SOFT_LINE_BREAK;
      lexer->result_symbol = SOFT_LINE_BREAK_MARKER;
      return true;
    }
  }
  return false;
}
