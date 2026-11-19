/** @file pong_core.h
 *  Pong match state and host-testable rules.
 */
#ifndef PONG_CORE_H
#define PONG_CORE_H

#include <stdbool.h>
#include <stdint.h>

/** Playable columns, including the two paddle columns. */
#define PONG_WIDTH 40
/** Playable rows between the horizontal walls. */
#define PONG_HEIGHT 16
/** Paddle length in terminal rows. */
#define PONG_PADDLE_SIZE 5
/** Points required to win a match. */
#define PONG_WIN_SCORE 5
/** Final level, with the shortest ball interval. */
#define PONG_MAX_LEVEL 10
/** Pause with the ball in the center before each serve. */
#define PONG_SERVE_MS 600

/** Opponent reaction settings, independent of the console interface. */
typedef enum { PONG_EASY, PONG_NORMAL, PONG_HARD } PongDifficulty;

/** Match phases visible to the console. */
typedef enum { PONG_SERVE, PONG_RALLY, PONG_WON, PONG_LOST } PongPhase;

/** Complete match state in terminal cells and milliseconds. */
typedef struct {
    int player_y;           /**< Top row of the player's paddle. */
    int cpu_y;              /**< Top row of the opponent's paddle. */
    int ball_x;            /**< Current ball column. */
    int ball_y;            /**< Current ball row. */
    int dx;                /**< Horizontal ball step, toward one paddle. */
    int dy;                /**< Vertical ball step, zero for a flat return. */
    uint16_t ball_wait_ms;  /**< Milliseconds to the next ball event. */
    uint16_t cpu_wait_ms;   /**< Milliseconds to the next CPU movement. */
    uint8_t player_score;   /**< Points won in this match by the player. */
    uint8_t cpu_score;      /**< Points won in this match by the CPU. */
    uint8_t level;          /**< Current level, beginning at one. */
    PongDifficulty difficulty; /**< Fixed difficulty for this session. */
    PongPhase phase;        /**< Serve, rally, or final result. */
} Pong;

/** Reset the entire session at level one. */
void pong_start(Pong *pong, PongDifficulty difficulty);

/** Move the player's paddle one row in the given signed direction. */
void pong_move(Pong *pong, int direction);

/** Run all ball and opponent events due within elapsed_ms. */
void pong_tick(Pong *pong, uint32_t elapsed_ms);

/** Start a fresh match at the next level after a win, if one remains. */
bool pong_next_level(Pong *pong);

/** Return the current number of milliseconds per ball cell. */
uint16_t pong_ball_interval(const Pong *pong);

#endif
