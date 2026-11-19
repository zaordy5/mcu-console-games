/** @file
 *  Shared layout format and input parsing interface.
 */
#pragma once

#include "board.h"

/** Longest side/mines/first/seed/type code, including the NUL terminator. */
enum { MINES_CODE_CAPACITY = sizeof "12/135/143/4294967295/2" };

/** Reproducible parameters for one mine distribution and its initial move. */
typedef struct {
    uint8_t side;       /**< Board width and height. */
    uint8_t mine_count; /**< Number of mines. */
    uint8_t first;      /**< Zero-based row-major index of the first move. */
    uint32_t seed;      /**< Board generator seed. */
    MinesLayout type;   /**< Placement family used by the generator. */
} MinesCode;

/** Parse a complete unsigned decimal mine count valid for the supplied board size. */
bool mines_parse_count(const char *input, unsigned side, uint8_t *count);
/** Parse a complete side/mines/first/seed/type code without truncation or overflow. */
bool mines_parse_code(const char *input, MinesCode *code);
