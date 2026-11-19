/** @file tetris_core.h
 *  Portable state and rules for the falling-blocks game.
 */
#ifndef TETRIS_CORE_H
/** Include guard for the portable game interface. */
#define TETRIS_CORE_H

#include <stdbool.h>
#include <stdint.h>

/** Width of the playable grid in cells. */
#define TETRIS_WIDTH 12
/** Height of the playable grid in cells. */
#define TETRIS_HEIGHT 18
/** Number of shapes in each shuffled bag. */
#define TETRIS_SHAPES 7

/** Starting gravity settings. */
typedef enum {
    TETRIS_EASY,
    TETRIS_NORMAL,
    TETRIS_HARD
} TetrisDifficulty;

/** State of a single falling-blocks session. */
typedef struct {
    uint8_t board[TETRIS_HEIGHT][TETRIS_WIDTH]; /**< Settled cell colors, zero for empty. */
    uint32_t score;        /**< Running score. */
    uint32_t lines;        /**< Total full rows removed. */
    uint32_t random;       /**< Nonzero xorshift state. */
    uint32_t until_drop;   /**< Milliseconds until the next gravity step. */
    uint16_t shape;        /**< Active shape bitmap in a row-major 4x4 box. */
    TetrisDifficulty difficulty; /**< Starting gravity setting. */
    int x;                 /**< Left edge of the active shape box. */
    int y;                 /**< Top edge of the active shape box. */
    uint8_t color;         /**< Active shape's nonzero color index. */
    uint8_t bag[TETRIS_SHAPES]; /**< Shuffled shape indices. */
    uint8_t next;          /**< Next position in the bag. */
    bool lost;             /**< A new piece could not enter the board. */
} Tetris;

/**
 * Clear a session and spawn the first piece.
 * A zero seed uses the default sequence; an unknown difficulty uses Normal.
 */
void tetris_reset(Tetris *game, TetrisDifficulty difficulty, uint32_t seed);

/** Move one column left (-1) or right (1) if the destination is free. */
bool tetris_move(Tetris *game, int direction);

/** Try a clockwise rotation around the active piece's center. */
bool tetris_rotate(Tetris *game);

/** Advance the piece one row immediately, locking it if blocked. */
bool tetris_soft_drop(Tetris *game);

/** Advance gravity by elapsed milliseconds and report a change to the board or piece. */
bool tetris_tick(Tetris *game, uint32_t elapsed_ms);

/** Return the displayed level, starting at 1. */
uint32_t tetris_level(const Tetris *game);

/** Return the current interval between automatic row steps in milliseconds. */
uint32_t tetris_interval(const Tetris *game);

/** Return the visible color, including the active piece, or zero outside the board. */
uint8_t tetris_cell(const Tetris *game, int x, int y);

#endif
