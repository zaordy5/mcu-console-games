/** @file pong_core.c
 *  Timed Pong mechanics independent of the terminal.
 */
#include "pong_core.h"

/** Top row for a vertically centered paddle. */
#define PADDLE_CENTER ((PONG_HEIGHT - PONG_PADDLE_SIZE) / 2)
/** Milliseconds per ball step at level one. */
#define BALL_INITIAL_MS 135
/** Reduction in the ball step interval at each successive level. */
#define BALL_LEVEL_STEP_MS 9
/** Maximum number of cells the CPU can project ahead. */
#define CPU_LOOKAHEAD 5

/** Move a paddle one cell while keeping all of it inside the field. */
static int paddle_move(int top, int direction)
{
    if (direction < 0 && top > 0) {
        return top - 1;
    }
    if (direction > 0 && top < PONG_HEIGHT - PONG_PADDLE_SIZE) {
        return top + 1;
    }
    return top;
}

/** Return the time between single-cell CPU movements. */
static uint16_t cpu_interval(PongDifficulty difficulty)
{
    switch (difficulty) {
    case PONG_EASY: return 600;
    case PONG_HARD: return 263;
    default: return 360;
    }
}

uint16_t pong_ball_interval(const Pong *pong)
{
    return BALL_INITIAL_MS - (pong->level - 1) * BALL_LEVEL_STEP_MS;
}

/** Center the field and set up a serve in the supplied direction. */
static void serve(Pong *pong, int direction)
{
    int points = pong->player_score + pong->cpu_score;
    pong->player_y = PADDLE_CENTER;
    pong->cpu_y = PADDLE_CENTER;
    pong->ball_x = PONG_WIDTH / 2;
    pong->ball_y = PONG_HEIGHT / 2 + points % 3 - 1;
    pong->dx = direction;
    pong->dy = (points + pong->level) % 2 ? -1 : 1;
    pong->ball_wait_ms = PONG_SERVE_MS;
    pong->cpu_wait_ms = cpu_interval(pong->difficulty);
    pong->phase = PONG_SERVE;
}

/** Reset points and prepare the first serve of the current level. */
static void new_match(Pong *pong)
{
    pong->player_score = 0;
    pong->cpu_score = 0;
    pong->last_point = PONG_NO_POINT;
    serve(pong, 1);
}

void pong_start(Pong *pong, PongDifficulty difficulty)
{
    pong->difficulty = difficulty;
    pong->level = 1;
    new_match(pong);
}

void pong_move(Pong *pong, int direction)
{
    if (pong->phase == PONG_SERVE || pong->phase == PONG_RALLY) {
        pong->player_y = paddle_move(pong->player_y, direction);
    }
}

bool pong_next_level(Pong *pong)
{
    if (pong->phase != PONG_WON || pong->level == PONG_MAX_LEVEL) {
        return false;
    }
    ++pong->level;
    new_match(pong);
    return true;
}

/** Award exactly one point and either prepare a serve or freeze the result. */
static void score_point(Pong *pong)
{
    if (pong->dx > 0) {
        pong->last_point = PONG_PLAYER_POINT;
        if (++pong->player_score == PONG_WIN_SCORE) {
            pong->phase = PONG_WON;
            return;
        }
    } else {
        pong->last_point = PONG_CPU_POINT;
        if (++pong->cpu_score == PONG_WIN_SCORE) {
            pong->phase = PONG_LOST;
            return;
        }
    }
    serve(pong, (pong->player_score + pong->cpu_score) % 2 ? -1 : 1);
}

/** Move one column at most, resolving wall and paddle contact in that order. */
static void ball_step(Pong *pong)
{
    int x = pong->ball_x + pong->dx;
    int y = pong->ball_y + pong->dy;
    if (y < 0 || y >= PONG_HEIGHT) {
        pong->dy = -pong->dy;
        y = pong->ball_y + pong->dy;
    }

    if (x == 0 || x == PONG_WIDTH - 1) {
        int top = x == 0 ? pong->player_y : pong->cpu_y;
        int offset = y - top;
        if (offset < 0 || offset >= PONG_PADDLE_SIZE) {
            score_point(pong);
            return;
        }
        pong->dx = -pong->dx;
        int middle = PONG_PADDLE_SIZE / 2;
        pong->dy = (offset > middle) - (offset < middle);
    } else {
        pong->ball_x = x;
    }
    pong->ball_y = y;
}

/** Predict the ball row up to five steps ahead, accounting for wall bounces. */
static int cpu_target(const Pong *pong)
{
    if (pong->dx < 0) {
        return PONG_HEIGHT / 2 - 1;
    }
    int distance = PONG_WIDTH - 1 - pong->ball_x;
    int steps = distance < CPU_LOOKAHEAD ? distance : CPU_LOOKAHEAD;
    int target = pong->ball_y + pong->dy * steps;
    if (target < 0) {
        return -target;
    }
    if (target >= PONG_HEIGHT) {
        return 2 * (PONG_HEIGHT - 1) - target;
    }
    return target;
}

/** Approach the projected ball row; recover toward the middle on a return. */
static void cpu_step(Pong *pong)
{
    int center = pong->cpu_y + PONG_PADDLE_SIZE / 2;
    int target = cpu_target(pong);
    int direction = (target > center) - (target < center);
    pong->cpu_y = paddle_move(pong->cpu_y, direction);
    pong->cpu_wait_ms = cpu_interval(pong->difficulty);
}

void pong_tick(Pong *pong, uint32_t elapsed_ms)
{
    while (elapsed_ms > 0 && (pong->phase == PONG_SERVE || pong->phase == PONG_RALLY)) {
        bool rally = pong->phase == PONG_RALLY;
        uint16_t wait = pong->ball_wait_ms;
        if (rally && pong->cpu_wait_ms < wait) {
            wait = pong->cpu_wait_ms;
        }
        if (elapsed_ms < wait) {
            wait = (uint16_t)elapsed_ms;
        }
        elapsed_ms -= wait;
        pong->ball_wait_ms -= wait;
        if (rally) {
            pong->cpu_wait_ms -= wait;
        }

        if (pong->ball_wait_ms == 0) {
            pong->ball_wait_ms = pong_ball_interval(pong);
            if (rally) {
                ball_step(pong);
            } else {
                pong->phase = PONG_RALLY;
            }
        }
        if (pong->phase == PONG_RALLY && pong->cpu_wait_ms == 0) {
            cpu_step(pong);
        }
    }
}
