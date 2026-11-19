#include "pong_core.h"

#include <assert.h>
#include <stdio.h>

/** Prepare a rally with the next step immediately due. */
static void place_ball(Pong *pong, int x, int y, int dx, int dy)
{
    pong->phase = PONG_RALLY;
    pong->ball_x = x;
    pong->ball_y = y;
    pong->dx = dx;
    pong->dy = dy;
    pong->ball_wait_ms = 1;
}

/** Exercise both limits of the two paddles. */
static void test_paddle_limits(void)
{
    Pong pong;
    pong_start(&pong, PONG_HARD);
    for (int n = 0; n < PONG_HEIGHT; ++n) {
        pong_move(&pong, -1);
    }
    assert(pong.player_y == 0);
    for (int n = 0; n < PONG_HEIGHT; ++n) {
        pong_move(&pong, 1);
    }
    assert(pong.player_y == PONG_HEIGHT - PONG_PADDLE_SIZE);

    place_ball(&pong, 10, 0, 1, 1);
    pong.ball_wait_ms = 30000;
    pong_tick(&pong, 4000);
    assert(pong.cpu_y == 0);
    pong.ball_y = PONG_HEIGHT - 1;
    pong_tick(&pong, 4000);
    assert(pong.cpu_y == PONG_HEIGHT - PONG_PADDLE_SIZE);
}

/** Check both wall rebounds, inclusive edges, and a corner at maximum speed. */
static void test_bounces(void)
{
    Pong pong;
    pong_start(&pong, PONG_NORMAL);
    place_ball(&pong, 10, 0, 1, -1);
    pong_tick(&pong, 1);
    assert(pong.ball_x == 11 && pong.ball_y == 1 && pong.dy == 1);
    place_ball(&pong, 10, PONG_HEIGHT - 1, 1, 1);
    pong_tick(&pong, 1);
    assert(pong.ball_y == PONG_HEIGHT - 2 && pong.dy == -1);

    pong.level = PONG_MAX_LEVEL;
    pong.player_y = 0;
    place_ball(&pong, 2, 1, -1, -1);
    pong.ball_wait_ms = pong_ball_interval(&pong);
    pong_tick(&pong, 3u * pong_ball_interval(&pong));
    assert(pong.ball_x == 2 && pong.dx == 1 && pong.cpu_score == 0);

    pong.cpu_y = 5;
    place_ball(&pong, PONG_WIDTH - 2, 8, 1, 1);
    pong_tick(&pong, 1);
    assert(pong.dx == -1 && pong.dy == 1 && pong.ball_y == 9);
    assert(pong.ball_x == PONG_WIDTH - 2 && pong.player_score == 0);
    place_ball(&pong, 1, 1, -1, -1);
    pong_tick(&pong, 1);
    assert(pong.dx == 1 && pong.dy == -1 && pong.ball_y == 0);
}

/** Check misses, a single point, the serve pause, results and level advancement. */
static void test_match(void)
{
    Pong pong;
    pong_start(&pong, PONG_NORMAL);
    assert(!pong_next_level(&pong));
    place_ball(&pong, 1, 0, -1, 1);
    pong_tick(&pong, PONG_SERVE_MS);
    assert(pong.cpu_score == 1 && pong.player_score == 0);
    assert(pong.phase == PONG_SERVE && pong.ball_x == PONG_WIDTH / 2);
    pong_tick(&pong, 1);
    assert(pong.phase == PONG_RALLY);
    pong_tick(&pong, pong_ball_interval(&pong));
    assert(pong.ball_x == PONG_WIDTH / 2 - 1 && pong.cpu_score == 1);

    pong.cpu_score = PONG_WIN_SCORE - 1;
    place_ball(&pong, 1, 0, -1, 1);
    pong_tick(&pong, 2000);
    assert(pong.phase == PONG_LOST && pong.cpu_score == PONG_WIN_SCORE);
    assert(!pong_next_level(&pong));

    pong_start(&pong, PONG_NORMAL);
    for (int point = 1; point <= PONG_WIN_SCORE; ++point) {
        place_ball(&pong, PONG_WIDTH - 2, 0, 1, 1);
        pong_tick(&pong, 1);
        assert(pong.player_score == point);
    }
    assert(pong.phase == PONG_WON && pong.cpu_score == 0);
    uint16_t first = pong_ball_interval(&pong);
    assert(pong_next_level(&pong));
    uint16_t second = pong_ball_interval(&pong);
    assert(pong.level == 2 && pong.phase == PONG_SERVE);
    assert(pong.player_score == 0 && pong.cpu_score == 0);
    place_ball(&pong, 15, 7, 1, 1);
    pong.ball_wait_ms = second;
    pong_tick(&pong, first - 1);
    assert(second < first && pong.ball_x == 16);
    printf("Ball moves one cell every %u ms at level 1, %u ms at level 2\n",
           (unsigned)first, (unsigned)second);

    pong.level = PONG_MAX_LEVEL;
    pong.phase = PONG_WON;
    assert(!pong_next_level(&pong));
    pong_start(&pong, PONG_EASY);
    assert(pong.level == 1 && pong.phase == PONG_SERVE && pong.cpu_score == 0);
}

/** Measure the first CPU movement from the start of a rally. */
static unsigned first_cpu_move(PongDifficulty difficulty)
{
    Pong pong;
    pong_start(&pong, difficulty);
    pong_tick(&pong, PONG_SERVE_MS);
    pong.ball_y = 0;
    pong.ball_wait_ms = 500;
    int start_y = pong.cpu_y;
    for (unsigned ms = 1; ms <= 500; ++ms) {
        pong_tick(&pong, 1);
        if (pong.cpu_y != start_y) {
            return ms;
        }
    }
    return 0;
}

/** Demand the same state for elapsed time delivered in one call or 1 ms at a time. */
static void test_elapsed_chunks(void)
{
    Pong whole, split;
    pong_start(&whole, PONG_NORMAL);
    pong_start(&split, PONG_NORMAL);
    pong_tick(&whole, 12000);
    for (unsigned ms = 0; ms < 12000; ++ms) {
        pong_tick(&split, 1);
    }
    assert(whole.player_score == split.player_score && whole.cpu_score == split.cpu_score);
    assert(whole.player_y == split.player_y && whole.cpu_y == split.cpu_y);
    assert(whole.ball_x == split.ball_x && whole.ball_y == split.ball_y);
    assert(whole.dx == split.dx && whole.dy == split.dy && whole.phase == split.phase);
    assert(whole.ball_wait_ms == split.ball_wait_ms && whole.cpu_wait_ms == split.cpu_wait_ms);
}

/** Run the mechanical regression checks on the host. */
int main(void)
{
    test_paddle_limits();
    test_bounces();
    test_match();
    test_elapsed_chunks();
    unsigned easy = first_cpu_move(PONG_EASY);
    unsigned normal = first_cpu_move(PONG_NORMAL);
    unsigned hard = first_cpu_move(PONG_HARD);
    assert(easy == 400 && normal == 240 && hard == 140);
    printf("First CPU move: Easy %u ms, Normal %u ms, Hard %u ms\n", easy, normal, hard);
    puts("Pong core checks passed");
}
