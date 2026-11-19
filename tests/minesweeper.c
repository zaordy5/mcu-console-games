#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "layout.h"
#include "minesweeper.h"

/** Last frame captured by the host UART substitute. */
static char frame[4096];

/** Capture a UART write and guard the fixed test buffer. */
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
    if (!strstr(frame, text)) {
        fprintf(stderr, "Expected %s in frame: %s\n", text, frame);
        assert(false);
    }
}

/** Verify exact mine counts and the entire safe region for every first cell. */
static void test_layouts(void)
{
    MinesBoard board;
    const unsigned densities[] = {12, 18, 25};
    for (int side = 8; side <= 12; side += 2) {
        for (unsigned d = 0; d < sizeof densities / sizeof densities[0]; ++d) {
            unsigned count = side * side * densities[d] / 100;
            for (int first = 0; first < side * side; ++first) {
                mines_board_reset(&board, side, count);
                mines_board_generate(&board, first, first, MINES_SCATTERED);
                assert(mines_neighbors(&board, first) == 0);
                assert(mines_open(&board, first) != MINES_LOST);
                unsigned placed = 0;
                for (int i = 0; i < side * side; ++i) {
                    placed += (board.cells[i] & CELL_MINE) != 0;
                    int dx = i % side - first % side;
                    int dy = i / side - first / side;
                    if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
                        assert((board.cells[i] & (CELL_OPEN | CELL_MINE)) == CELL_OPEN);
                    }
                }
                assert(placed == count);
            }
        }
    }
}

/** Verify zero expansion, flag protection, hints, and victory. */
static void test_reveal(void)
{
    MinesBoard board;
    mines_board_reset(&board, 8, 1);
    assert(mines_flag(&board, 1) && board.flagged == 1);

    board.generated = true;
    board.cells[63] |= CELL_MINE;
    assert(mines_question(&board, 2));
    assert(mines_open(&board, 1) == MINES_NO_CHANGE);
    assert(mines_neighbors(&board, 54) == 1);
    assert(mines_neighbors(&board, 0) == 0);
    assert(mines_open(&board, 0) == MINES_OPENED);
    assert(board.remaining == 1 && board.cells[2] == CELL_OPEN);
    assert(!mines_flag(&board, 0));
    assert(mines_flag(&board, 1) && board.flagged == 0);
    assert(mines_open(&board, 1) == MINES_WON);
    assert(mines_open(&board, 1) == MINES_NO_CHANGE);
}

/** Check chording with missing, correct and incorrect flags. */
static void test_chord(void)
{
    MinesBoard board;
    mines_board_reset(&board, 8, 1);
    board.generated = true;
    board.cells[0] = CELL_MINE;
    assert(mines_open(&board, 9) == MINES_OPENED);
    assert(mines_open(&board, 9) == MINES_NO_CHANGE);
    assert(mines_flag(&board, 0));
    assert(mines_question(&board, 10));
    assert(mines_open(&board, 9) == MINES_WON);
    assert(board.cells[10] == CELL_OPEN);
    assert(board.hit == MINES_NO_HIT);

    mines_board_reset(&board, 8, 1);
    board.generated = true;
    board.cells[0] = CELL_MINE;
    assert(mines_open(&board, 9) == MINES_OPENED);
    assert(mines_flag(&board, 1));
    assert(mines_open(&board, 9) == MINES_LOST);
    assert(board.hit == 0);
}

/** Verify the solver accepts clearable boards and rejects ambiguous boards. */
static void test_solver(void)
{
    MinesBoard board;
    unsigned solvable = 0;
    for (unsigned seed = 1; seed <= 128; ++seed) {
        mines_board_reset(&board, 12, 36);
        mines_board_generate(&board, 78, seed * 0x9e3779b9u, MINES_SCATTERED);
        solvable += mines_board_solvable(&board, 78);
        assert(board.remaining == 108 && board.flagged == 0);
    }
    assert(solvable > 0 && solvable < 128);
}

/** Check every first cell, supported Logic count, and fallback orientation. */
static void test_compact_layouts(void)
{
    MinesBoard board;
    for (unsigned side = 8; side <= 12; side += 2) {
        for (unsigned count = 1; count <= side * side / 4; ++count) {
            for (unsigned first = 0; first < side * side; ++first) {
                for (unsigned orientation = 0; orientation < 4; ++orientation) {
                    mines_board_reset(&board, side, count);
                    mines_board_generate(&board, first, orientation, MINES_COMPACT);
                    assert(mines_neighbors(&board, first) == 0);
                    assert(mines_board_solvable(&board, first));
                }
            }
        }
    }
}

/** Move the cursor to a cell and update the test cursor coordinates. */
static void go_to(unsigned index, unsigned *x, unsigned *y)
{
    unsigned next_x = index % 8;
    unsigned next_y = index / 8;
    while (*x < next_x) { minesweeper_game.key('d'); ++*x; }
    while (*x > next_x) { minesweeper_game.key('a'); --*x; }
    while (*y < next_y) { minesweeper_game.key('s'); ++*y; }
    while (*y > next_y) { minesweeper_game.key('w'); --*y; }
}

/** Follow the first field through victory, a new level, and return. */
static void test_session(void)
{
    MinesBoard expected;
    mines_board_reset(&expected, 8, 7);
    mines_board_generate(&expected, 0, 0x9e3779b9u, MINES_SCATTERED);

    minesweeper_game.start(GAME_EASY);
    expect_frame("L1/3  Easy  Classic");
    assert(strstr(frame, "Mines 7  Left 7  Time 0s  Cell A1"));
    minesweeper_game.key(' ');
    unsigned x = 0, y = 0;
    for (unsigned i = 0; i < 64; ++i) {
        if (!(expected.cells[i] & CELL_MINE)) {
            go_to(i, &x, &y);
            minesweeper_game.key(' ');
        }
    }
    expect_frame("Level cleared!");
    minesweeper_game.key('n');
    expect_frame("L2/3  Easy  Classic");
    assert(strstr(frame, "Mines 12  Left 12  Time 0s  Cell A1"));
    minesweeper_game.key('q');
    assert(minesweeper_game.finished());
}

/** Verify arrows, flagged counts, timer, loss marks and exact replay. */
static void test_loss_and_replay(void)
{
    const unsigned seed = 0x3c6ef372u;
    MinesBoard expected;
    mines_board_reset(&expected, 8, 16);
    mines_board_generate(&expected, 0, seed, MINES_SCATTERED);
    unsigned mine = 0;
    while (!(expected.cells[mine] & CELL_MINE)) {
        ++mine;
    }

    minesweeper_game.start(GAME_HARD);
    minesweeper_game.key(' ');
    expect_frame("Mines 16  Left 16  Time 0s  Cell A1");
    char first_frame[sizeof frame];
    strcpy(first_frame, frame);
    minesweeper_game.tick(1200);
    expect_frame("Time 1s  Cell A1");
    assert(!strstr(frame, "WASD"));  /* A timer tick updates only the header. */

    minesweeper_game.key('\033');
    minesweeper_game.key('[');
    minesweeper_game.key('C');
    unsigned x = 1, y = 0;
    go_to(63, &x, &y);
    unsigned wrong = 63;
    mines_open(&expected, 0);
    while (expected.cells[wrong] & (CELL_OPEN | CELL_MINE)) {
        assert(wrong > 0);
        --wrong;
    }
    go_to(wrong, &x, &y);
    expect_frame("L1/3  Hard");
    assert(strstr(frame, "[.]"));
    minesweeper_game.key('f');
    expect_frame("Left 15");
    assert(strstr(frame, "[F]"));
    go_to(mine, &x, &y);
    minesweeper_game.key(' ');
    expect_frame("Mine hit!");
    assert(strstr(frame, "[!]") && strstr(frame, "\033[91m X "));
    assert(strstr(frame, "Layout 8/16/0/1013904242/0"));
    minesweeper_game.tick(2000);
    expect_frame("");
    assert(frame[0] == '\0');
    minesweeper_game.key('b');
    expect_frame("Left 16  Time 0s");
    assert(strcmp(frame, first_frame) == 0);
}

/** Verify that opting into Logic mode starts with a solver-approved layout. */
static void test_logic_session(void)
{
    minesweeper_game.start(GAME_HARD);
    minesweeper_game.key('g');
    expect_frame("Hard  Logic");
    minesweeper_game.key(' ');
    expect_frame("Checking layouts");
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        minesweeper_game.tick(1);
    }
    expect_frame("\033[30;107m[ ]");
    assert(!strstr(frame, "Layout search failed"));
}

/** Validate input boundaries and the last safe starting square. */
static void test_code_parser(void)
{
    MinesCode code;
    uint8_t count;
    assert(mines_parse_code("12/135/143/4294967295/2", &code));
    assert(code.side == 12 && code.mine_count == 135 && code.first == 143 &&
           code.seed == UINT32_MAX && code.type == MINES_BANDED);
    assert(!mines_parse_code("12/135/143/4294967296/2", &code));
    assert(!mines_parse_code("12/136/143/1/0", &code));
    assert(!mines_parse_code("12/135/144/1/0", &code));
    assert(!mines_parse_code("9/12/0/0/0", &code));
    assert(!mines_parse_code("8/17//1/0", &code));
    assert(!mines_parse_code("8/17/0/1", &code));
    assert(!mines_parse_code("8/17/0/1/3", &code));
    assert(!mines_parse_code("8/17/0/1/0 extra", &code));
    assert(!mines_parse_code("", &code));
    assert(mines_parse_count("55", 8, &count) && count == 55);
    assert(!mines_parse_count("56", 8, &count));
    assert(!mines_parse_count("0", 8, &count));
    assert(!mines_parse_count("42949672960", 8, &count));
}

/** Send a sequence of UART bytes to the active game. */
static void send_text(const char *input)
{
    while (*input) {
        minesweeper_game.key(*input++);
    }
}

/** Check a custom game's validation, board size and valid controls. */
static void test_custom_session(void)
{
    minesweeper_game.start(GAME_NORMAL);
    send_text("c3");
    expect_frame("Enter mine count (1-135)");
    send_text("136\r\n");
    expect_frame("outside the allowed range");
    send_text("\b\b\b28\r\n");
    expect_frame("Custom  Classic");
    assert(strstr(frame, "Mines 28  Left 28  Time 0s  Cell A1"));
    assert(strstr(frame, " K  L "));
    assert(strstr(frame, "\033[30;107m[.]"));
    minesweeper_game.key('?');
    expect_frame("\033[30;107m[?]");
    assert(strstr(frame, "Left 28"));
    minesweeper_game.key('f');
    expect_frame("[F]");
    assert(strstr(frame, "Left 27"));
    minesweeper_game.key('?');
    expect_frame("[?]");
    assert(strstr(frame, "Left 28"));
    send_text("g ");
    expect_frame("Checking layouts");
    for (unsigned i = 0; i < 128; ++i) {
        minesweeper_game.tick(1);
    }
    expect_frame("Custom  Logic");
    assert(strstr(frame, "\033[30;107m[ ]"));
    minesweeper_game.key('q');
}

/** Test a pasted shared code, session timing, and resume from the common menu. */
static void test_shared_and_pause(void)
{
    minesweeper_game.start(GAME_HARD);
    minesweeper_game.key('l');
    expect_frame("Load layout");
    send_text("12/135/143/4294967295/29\r");
    expect_frame("Invalid layout code.");
    send_text("\bq");

    // The closing bit is deliberately delivered as an ANSI bracketed-paste wrapper.
    send_text("l\033[200~8/16/0/1013904242/0\033[201~\r\n");
    expect_frame("Shared layout");
    assert(strstr(frame, "Mines 16  Left 16  Time 0s  Cell A1"));
    assert(strstr(frame, "\033[94m 1 ") && strstr(frame, "\033[92m 2 "));

    send_text("\033[1;2C");
    expect_frame("Cell B1");
    assert(strstr(frame, "\033[30;107m[ ]"));
    assert(!strstr(frame, " A  B  C "));  /* Moving only repaints the changed cells. */
    minesweeper_game.tick(1200);
    expect_frame("Time 1s  Cell B1");

    minesweeper_game.key('p');
    expect_frame("Minesweeper paused");
    assert(strstr(frame, "Time 1s") && !strstr(frame, " A  B  C "));
    minesweeper_game.tick(5000);
    expect_frame("");
    assert(frame[0] == '\0');
    minesweeper_game.key('p');
    expect_frame("Time 1s  Cell B1");

    send_text("pq");
    assert(minesweeper_game.finished());
    minesweeper_game.start(GAME_EASY);
    expect_frame("U continue");
    minesweeper_game.key('u');
    expect_frame("Shared layout");
    assert(strstr(frame, "Left 16  Time 1s  Cell B1"));
    minesweeper_game.tick(1000);
    expect_frame("Time 2s  Cell B1");
    minesweeper_game.key('q');
}

/** Print a reproducible mine layout from the result screen's Layout code. */
static int print_layout(const char *input)
{
    MinesCode code;
    if (!mines_parse_code(input, &code)) {
        fputs("Expected Layout code: side/mines/first/seed/type\n", stderr);
        return 1;
    }
    MinesBoard board;
    mines_board_reset(&board, code.side, code.mine_count);
    mines_board_generate(&board, code.first, code.seed, code.type);
    for (unsigned y = 0; y < code.side; ++y) {
        for (unsigned x = 0; x < code.side; ++x) {
            unsigned index = y * code.side + x;
            putchar(index == code.first ? '@' : (board.cells[index] & CELL_MINE) ? '*' : '.');
        }
        putchar('\n');
    }
    return 0;
}

/** Run the regression suite or reconstruct a board from a Layout code. */
int main(int argc, char **argv)
{
    if (argc == 2) {
        return print_layout(argv[1]);
    }
    test_layouts();
    test_reveal();
    test_chord();
    test_solver();
    test_compact_layouts();
    test_session();
    test_loss_and_replay();
    test_logic_session();
    test_code_parser();
    test_custom_session();
    test_shared_and_pause();
    puts("Minesweeper checks passed");
}
