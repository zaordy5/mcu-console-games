#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Field capacity and sentinel for an absent detonated cell. */
enum {
    MINES_MIN_SIDE = 8,
    MINES_MAX_SIDE = 12,
    MINES_SAFE_AREA = 9,
    MINES_MAX_CELLS = MINES_MAX_SIDE * MINES_MAX_SIDE,
    MINES_NO_HIT = MINES_MAX_CELLS
};

/** Cell flags. Unopened mines are rendered as ordinary hidden cells. */
enum { CELL_MINE = 1, CELL_OPEN = 2, CELL_FLAG = 4, CELL_QUESTION = 8 };

/** Result of an attempt to open a cell or its neighbors. */
typedef enum { MINES_NO_CHANGE, MINES_OPENED, MINES_LOST, MINES_WON } MinesResult;

/** Stable placement codes stored in shared layouts. */
typedef enum { MINES_SCATTERED = 0, MINES_COMPACT = 1, MINES_BANDED = 2 } MinesLayout;

/** Field state for a single level, addressed by row-major cell indices. */
typedef struct {
    uint8_t cells[MINES_MAX_CELLS]; /**< Bit flags for each active cell. */
    uint8_t side;                  /**< Width and height in cells. */
    uint8_t mine_count;            /**< Exact number of mines to place. */
    uint8_t remaining;             /**< Number of safe cells still closed. */
    uint8_t flagged;               /**< Number of flags currently placed. */
    uint8_t hit;                   /**< Detonated cell or MINES_NO_HIT. */
    bool generated;                /**< Whether mines have been placed. */
} MinesBoard;

/** Validate a supported size and mine count, allowing a safe opening anywhere. */
bool mines_board_valid(unsigned side, unsigned mine_count);
/** Logic mode supports layouts up to 25 percent mine density. */
bool mines_board_logic_allowed(const MinesBoard *board);
/** Reset the field for one of the supported square sizes (8, 10 or 12). */
void mines_board_reset(MinesBoard *board, uint8_t side, uint8_t mine_count);
/** Place mines outside the first cell and all of its neighbors using the given seed. */
void mines_board_generate(MinesBoard *board, uint8_t first, uint32_t seed, MinesLayout layout);
/** Count mines adjacent to a valid cell, excluding the cell itself. */
uint8_t mines_neighbors(const MinesBoard *board, uint8_t index);
/** Toggle a flag on a closed cell. Return true if the flag changed. */
bool mines_flag(MinesBoard *board, uint8_t index);
/** Toggle a question mark on a hidden cell, replacing any flag. */
bool mines_question(MinesBoard *board, uint8_t index);
/** Open a cell, or chord around an open number with a matching flag count. */
MinesResult mines_open(MinesBoard *board, uint8_t index);
/** Check that starting at first can clear this layout using logical deductions. */
bool mines_board_solvable(const MinesBoard *board, uint8_t first);
