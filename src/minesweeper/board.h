#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Maximum field side across all levels. */
enum { MINES_MAX_SIDE = 12, MINES_MAX_CELLS = MINES_MAX_SIDE * MINES_MAX_SIDE };

/** Cell flags. Unopened mines must be rendered as ordinary hidden cells. */
enum { CELL_MINE = 1, CELL_OPEN = 2, CELL_FLAG = 4 };

/** Result of an attempt to open a cell. */
typedef enum { MINES_NO_CHANGE, MINES_OPENED, MINES_LOST, MINES_WON } MinesResult;

/** Field state for a single level, addressed by row-major cell indices. */
typedef struct {
    uint8_t cells[MINES_MAX_CELLS]; /**< Bit flags for each active cell. */
    uint8_t side;                  /**< Width and height in cells. */
    uint8_t mine_count;            /**< Exact number of mines to place. */
    uint8_t remaining;             /**< Number of safe cells still closed. */
    bool generated;                /**< Whether the first open generated the field. */
} MinesBoard;

/** Reset the field for one of the supported square sizes (8, 10 or 12). */
void mines_board_reset(MinesBoard *board, uint8_t side, uint8_t mine_count);
/** Count mines adjacent to a valid cell, excluding the cell itself. */
uint8_t mines_neighbors(const MinesBoard *board, uint8_t index);
/** Toggle a flag on a closed cell. Return true if the flag changed. */
bool mines_flag(MinesBoard *board, uint8_t index);
/** Open a valid cell. On the first open, generate a layout excluding that cell. */
MinesResult mines_open(MinesBoard *board, uint8_t index, uint32_t seed);
