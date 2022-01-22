#ifndef TREE_SITTER_MDSCANNER_STATE_H_
#define TREE_SITTER_MDSCANNER_STATE_H_

#include <stdint.h>

// State bitflags used with `Scanner.state`

// Currently matching (at the beginning of a line)
const uint16_t STATE_MATCHING = 0x1 << 0;
// Last line break was inside a paragraph
const uint16_t STATE_WAS_SOFT_LINE_BREAK = 0x1 << 1;
// TODO
const uint16_t STATE_EMPHASIS_DELIMITER_MOD_3 = 0x3 << 2;
// Current delimiter run is opening
const uint16_t STATE_EMPHASIS_DELIMITER_IS_OPEN = 0x1 << 4;
// Number of consecutive SPLIT_TOKEN emitted
const uint16_t STATE_SPLIT_TOKEN_COUNT = 0x3 << 5;
// Block should be closed after next line break
const uint16_t STATE_CLOSE_BLOCK = 0x1 << 7;
// Paragraph was just closed. Error if no new block opens
const uint16_t STATE_NEED_OPEN_BLOCK = 0x1 << 8;
// Basically the same as STATE_NEED_OPEN_BLOCK. I am actually not sure about the
// difference. This shoudl be investigated.
const uint16_t STATE_JUST_CLOSED = 0x1 << 9;

#endif
