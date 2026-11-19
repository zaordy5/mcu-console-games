#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "minesweeper.h"

/** Last frame captured by the host UART substitute. */
static char frame[4096];

/** Capture a single UART write and guard the fixed test buffer. */
void console_write(const char *text)
{
    assert(strlen(frame) + strlen(text) < sizeof frame);
    strcat(frame, text);
}

/** Draw a fresh frame and assert that the supplied text appears. */
static void expect_frame(const char *text)
{
    frame[0] = '\0';
    minesweeper_game.draw();
    assert(strstr(frame, text));
}

/** Verify all configured mine counts and the first-click guarantee. */
static void test_layouts(void)
{
    MinesBoard board;
    const unsigned densities[] = {12, 18, 25};
    for (unsigned side = 8; side <= 12; side += 2) {
        for (unsigned d = 0; d < sizeof densities / sizeof densities[0]; ++d) {
            unsigned count = side * side * densities[d] / 100;
            for (unsigned first = 0; first < side * side; ++first) {
                mines_board_reset(&board, side, count);
                assert(mines_open(&board, first, first) != MINES_LOST);
                assert((board.cells[first] & (CELL_MINE | CELL_OPEN)) == CELL_OPEN);
                unsigned placed = 0;
                for (unsigned i = 0; i < side * side; ++i) {
                    placed += (board.cells[i] & CELL_MINE) != 0;
                }
                assert(placed == count);
            }
        }
    }
}

/** Verify corner hints, flag protection, flood, loss, and clearing the board. */
static void test_reveal(void)
{
    MinesBoard board;
    mines_board_reset(&board, 8, 1);
    assert(mines_flag(&board, 1));
    assert(mines_open(&board, 1, 1) == MINES_NO_CHANGE);
    assert(!board.generated);

    board.generated = true;
    board.cells[63] |= CELL_MINE;
    assert(mines_neighbors(&board, 54) == 1);
    assert(mines_neighbors(&board, 0) == 0);
    assert(mines_open(&board, 0, 1) == MINES_OPENED);
    assert(board.remaining == 1);
    assert(!mines_flag(&board, 0));
    assert(mines_flag(&board, 1));
    assert(mines_open(&board, 1, 1) == MINES_WON);
    assert(mines_open(&board, 1, 1) == MINES_NO_CHANGE);
    assert(board.remaining == 0);

    mines_board_reset(&board, 8, 1);
    board.generated = true;
    board.cells[63] |= CELL_MINE;
    assert(mines_open(&board, 63, 0) == MINES_LOST);
}

/** Follow the first field through victory, new level, and return. */
static void test_session(void)
{
    MinesBoard expected;
    mines_board_reset(&expected, 8, 7);
    mines_open(&expected, 0, 0x9e3779b9u);

    minesweeper_game.start(GAME_EASY);
    expect_frame("Level 1/3  Easy  Mines 7");
    minesweeper_game.key(' ');
    for (unsigned y = 0; y < 8; ++y) {
        if (y != 0) {
            minesweeper_game.key('s');
        }
        for (unsigned i = 0; i < 8; ++i) {
            unsigned x = y % 2 == 0 ? i : 7 - i;
            if (i != 0) {
                minesweeper_game.key(y % 2 == 0 ? 'd' : 'a');
            }
            if (!(expected.cells[8 * y + x] & CELL_MINE)) {
                minesweeper_game.key(' ');
            }
        }
    }
    expect_frame("Level cleared!");
    minesweeper_game.key('n');
    expect_frame("Level 2/3  Easy  Mines 12");
    minesweeper_game.key('q');
    assert(minesweeper_game.finished());

    minesweeper_game.start(GAME_HARD);
    assert(!minesweeper_game.finished());
    expect_frame("Level 1/3  Hard  Mines 16");
}

int main(void)
{
    test_layouts();
    test_reveal();
    test_session();
    puts("Minesweeper checks passed");
}
