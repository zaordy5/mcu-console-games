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
/** Side length of a piece bitmap and of the next-piece preview. */
#define TETRIS_PIECE_SIZE 4
/** Base score multiplied by the square of the number of cleared lines. */
#define TETRIS_LINE_SCORE 100u
/** Score for clearing four lines with one piece. */
#define TETRIS_QUAD_SCORE (TETRIS_LINE_SCORE * 16u)

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
    uint32_t quad_clears;  /**< Number of times one piece cleared four rows. */
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

/** Turn clockwise, nudging inwards only if the turned shape crosses a side wall. */
bool tetris_rotate(Tetris *game);

/** Advance the piece one row immediately, locking it if blocked. */
bool tetris_soft_drop(Tetris *game);

/** Return the lowest legal top row for the active piece. */
int tetris_landing_y(const Tetris *game);

/** Immediately place the current piece at its landing position and spawn the next. */
bool tetris_hard_drop(Tetris *game);

/** Advance gravity by elapsed milliseconds and report a change to the board or piece. */
bool tetris_tick(Tetris *game, uint32_t elapsed_ms);

/** Return the displayed level, starting at 1. */
uint32_t tetris_level(const Tetris *game);

/** Return the current interval between automatic row steps in milliseconds. */
uint32_t tetris_interval(const Tetris *game);

/** Return the visible color, including the active piece, or zero outside the board. */
uint8_t tetris_cell(const Tetris *game, int x, int y);

/** Return whether the active piece covers a cell if placed at the given top row. */
bool tetris_piece_at(const Tetris *game, int x, int y, int top);

/** Return the next piece's color, or zero after game over. */
uint8_t tetris_next_color(const Tetris *game);

/** Return the next piece's visible color at a preview coordinate, or zero if empty. */
uint8_t tetris_next_cell(const Tetris *game, int x, int y);

#endif
