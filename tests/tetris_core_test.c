/** @file tetris_core_test.c
 *  Host checks for movement, locking, row removal, and gravity.
 */
#include "tetris_core.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

/** Three-cell bar oriented horizontally in the middle row. */
#define BAR_HORIZONTAL 0x038u
/** Three-cell bar oriented vertically in the middle column. */
#define BAR_VERTICAL 0x092u

/** Put a three-cell bar in a known position on an empty board. */
static void place_bar(Tetris *game, uint16_t shape, int x, int y)
{
    tetris_reset(game, TETRIS_NORMAL, 1);
    game->shape = shape;
    game->color = 1;
    game->x = x;
    game->y = y;
}

/** Fill a test row except for the inclusive span occupied by the active piece. */
static void fill_except(Tetris *game, int y, int first_gap, int last_gap)
{
    for (int x = 0; x < TETRIS_WIDTH; ++x) {
        game->board[y][x] = (uint8_t)(x < first_gap || x > last_gap ? 2 : 0);
    }
}

/** Check both walls, the floor, occupied cells, and a successful rotation. */
static void test_movement_and_rotation(void)
{
    Tetris game;
    place_bar(&game, BAR_HORIZONTAL, 0, 3);
    assert(!tetris_move(&game, -1) && game.x == 0);
    assert(tetris_move(&game, 1) && game.x == 1);
    game.board[4][4] = 2;
    assert(!tetris_move(&game, 1) && game.x == 1);
    game.board[5][2] = 2;
    assert(!tetris_rotate(&game) && game.shape == BAR_HORIZONTAL);
    game.board[5][2] = 0;
    assert(tetris_rotate(&game) && game.shape == BAR_VERTICAL);
    assert(tetris_cell(&game, 2, 3) == 1 && game.board[3][2] == 0);
    assert(tetris_move(&game, -1));
    assert(tetris_move(&game, -1) && game.x == -1);
    assert(!tetris_move(&game, -1));
    assert(!tetris_rotate(&game) && game.shape == BAR_VERTICAL && game.x == -1);

    place_bar(&game, BAR_HORIZONTAL, TETRIS_WIDTH - 3, TETRIS_HEIGHT - 2);
    assert(!tetris_move(&game, 1) && game.x == TETRIS_WIDTH - 3);
    assert(!tetris_rotate(&game) && game.shape == BAR_HORIZONTAL);
    puts("PASS movement and rotation: walls, floor, settled cells");
}

/** Check that gravity settles pieces on the board floor and on other blocks. */
static void test_lock(void)
{
    Tetris game;
    place_bar(&game, BAR_HORIZONTAL, 3, TETRIS_HEIGHT - 3);
    assert(tetris_tick(&game, 600));
    assert(game.y == TETRIS_HEIGHT - 2 && game.board[TETRIS_HEIGHT - 1][3] == 0);
    assert(tetris_tick(&game, 600));
    assert(game.y == 0 && !game.lost);
    for (int x = 3; x <= 5; ++x) {
        assert(game.board[TETRIS_HEIGHT - 1][x] == 1);
    }

    place_bar(&game, BAR_HORIZONTAL, 3, 6);
    game.board[8][4] = 4;
    assert(tetris_soft_drop(&game) && game.y == 0);
    assert(game.board[7][3] == 1 && game.board[8][4] == 4);
    puts("PASS locking: floor and settled stack");
}

/** Check single and non-adjacent row clears, compaction, score, and empty top rows. */
static void test_clear_rows(void)
{
    Tetris game;
    place_bar(&game, BAR_HORIZONTAL, 4, TETRIS_HEIGHT - 2);
    fill_except(&game, TETRIS_HEIGHT - 1, 4, 6);
    game.board[TETRIS_HEIGHT - 2][0] = 7;
    assert(tetris_soft_drop(&game));
    assert(game.lines == 1 && game.score == 100);
    assert(game.board[TETRIS_HEIGHT - 1][0] == 7 && game.board[TETRIS_HEIGHT - 1][4] == 0);

    place_bar(&game, BAR_VERTICAL, 4, TETRIS_HEIGHT - 3);
    fill_except(&game, TETRIS_HEIGHT - 3, 5, 5);
    fill_except(&game, TETRIS_HEIGHT - 1, 5, 5);
    game.board[TETRIS_HEIGHT - 2][0] = 6;
    game.board[TETRIS_HEIGHT - 4][1] = 7;
    game.board[0][0] = 3;
    assert(tetris_soft_drop(&game));
    assert(game.lines == 2 && game.score == 400 && !game.lost);
    assert(game.board[TETRIS_HEIGHT - 1][0] == 6 && game.board[TETRIS_HEIGHT - 1][5] == 1);
    assert(game.board[TETRIS_HEIGHT - 2][1] == 7);
    assert(game.board[0][0] == 0 && game.board[1][0] == 0 && game.board[2][0] == 3);

    place_bar(&game, BAR_VERTICAL, 4, TETRIS_HEIGHT - 3);
    for (int y = TETRIS_HEIGHT - 3; y < TETRIS_HEIGHT; ++y) {
        fill_except(&game, y, 5, 5);
    }
    assert(tetris_soft_drop(&game));
    assert(game.lines == 3 && game.score == 900 && game.board[TETRIS_HEIGHT - 1][5] == 0);
    puts("PASS line clearing: one, two separated, and three adjacent rows");
}

/** Check blocked spawning, frozen game over, a clean restart, and the shuffled bag. */
static void test_loss_and_restart(void)
{
    Tetris game;
    place_bar(&game, BAR_HORIZONTAL, 3, TETRIS_HEIGHT - 2);
    for (int y = 0; y < 3; ++y) {
        game.board[y][TETRIS_WIDTH / 2 - 2] = 2;
        game.board[y][TETRIS_WIDTH / 2 - 1] = 2;
        game.board[y][TETRIS_WIDTH / 2] = 2;
    }
    game.score = 400;
    game.lines = 5;
    assert(tetris_soft_drop(&game) && game.lost);
    assert(!tetris_move(&game, -1) && !tetris_rotate(&game));
    assert(!tetris_soft_drop(&game) && !tetris_tick(&game, 1000));
    assert(game.score == 400 && tetris_cell(&game, game.x, game.y) == 2);

    tetris_reset(&game, TETRIS_HARD, 1);
    assert(!game.lost && game.score == 0 && game.lines == 0 && game.y == 0);
    assert(tetris_interval(&game) == 350 && game.until_drop == 350);
    assert(game.next == 1);
    for (int y = 0; y < TETRIS_HEIGHT; ++y) {
        for (int x = 0; x < TETRIS_WIDTH; ++x) {
            assert(game.board[y][x] == 0);
        }
    }
    uint16_t first_shape = game.shape;
    unsigned seen = 0;
    for (unsigned i = 0; i < TETRIS_SHAPES; ++i) {
        assert(game.bag[i] < TETRIS_SHAPES);
        seen |= 1u << game.bag[i];
    }
    assert(seen == (1u << TETRIS_SHAPES) - 1u);
    assert(tetris_tick(&game, UINT32_MAX) && game.lost);
    tetris_reset(&game, TETRIS_HARD, 1);
    assert(game.shape == first_shape && !game.lost);
    puts("PASS blocked spawn, frozen result, and clean restart");
}

/** Check real row-step timings on three difficulties and after the level threshold. */
static void test_gravity(void)
{
    Tetris easy;
    Tetris normal;
    Tetris hard;
    tetris_reset(&easy, TETRIS_EASY, 0);
    tetris_reset(&normal, TETRIS_NORMAL, 0);
    tetris_reset(&hard, TETRIS_HARD, 0);
    assert(!tetris_tick(&easy, 349) && !tetris_tick(&normal, 349) && !tetris_tick(&hard, 349));
    assert(!tetris_tick(&easy, 1) && !tetris_tick(&normal, 1) && tetris_tick(&hard, 1));
    assert(easy.y == 0 && normal.y == 0 && hard.y == 1);
    assert(!tetris_tick(&easy, 250) && tetris_tick(&normal, 250));
    assert(!tetris_tick(&easy, 249) && tetris_tick(&easy, 1));
    assert(easy.y == 1 && normal.y == 1);
    assert(tetris_tick(&easy, 1800) && easy.y == 3 && easy.until_drop == 750);

    place_bar(&normal, BAR_HORIZONTAL, 4, TETRIS_HEIGHT - 2);
    normal.lines = 4;
    fill_except(&normal, TETRIS_HEIGHT - 1, 4, 6);
    assert(tetris_level(&normal) == 1 && tetris_interval(&normal) == 600);
    assert(tetris_tick(&normal, 610));
    assert(normal.lines == 5 && tetris_level(&normal) == 2 && tetris_interval(&normal) == 530);
    assert(normal.y == 0 && normal.until_drop == 520);
    assert(!tetris_tick(&normal, 519) && tetris_tick(&normal, 1) && normal.y == 1);
    assert(tetris_soft_drop(&normal) && normal.y == 2 && normal.until_drop == 530);
    normal.lines = 500;
    assert(tetris_interval(&normal) == 100);
    puts("PASS gravity: Easy 850 ms, Normal 600 ms, Hard 350 ms; Normal level 2: 530 ms");
}

/** Check out-of-range lookups and unknown starting difficulty. */
static void test_api_boundaries(void)
{
    Tetris game;
    tetris_reset(&game, (TetrisDifficulty)-1, 0);
    assert(game.difficulty == TETRIS_NORMAL && game.until_drop == 600);
    tetris_reset(&game, (TetrisDifficulty)100, 0);
    assert(game.difficulty == TETRIS_NORMAL && game.until_drop == 600);
    assert(tetris_cell(&game, INT_MIN, 0) == 0);
    assert(tetris_cell(&game, 0, INT_MIN) == 0);
    assert(tetris_cell(&game, TETRIS_WIDTH, 0) == 0);
    assert(tetris_cell(&game, 0, TETRIS_HEIGHT) == 0);
    assert(!tetris_move(&game, INT_MAX));
    puts("PASS API boundaries: invalid difficulty, coordinates, and direction");
}

/** Run all portable game checks. */
int main(void)
{
    test_movement_and_rotation();
    test_lock();
    test_clear_rows();
    test_loss_and_restart();
    test_gravity();
    test_api_boundaries();
    return 0;
}
