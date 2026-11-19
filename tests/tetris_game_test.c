/** @file tetris_game_test.c
 *  Game callback checks with a host-side USART2 substitute.
 */
#include "tetris.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/** Retrowave background of an empty board cell. */
#define FIELD_BG "\033[48;2;36;22;56m"
/** Next background on a new board, 0xC2D9FF for the I piece. */
#define I_BG "\033[48;2;194;217;255m"
/** Background of the first active piece, 0xFF85C7 for the T piece. */
#define T_BG "\033[48;2;255;133;199m"
/** Reset required before the shared menu takes over. */
#define RESET_STYLE "\033[0m"

/** Captured terminal bytes for one action or frame. */
static char output[4096];
/** Number of captured bytes before the terminating NUL. */
static size_t used;

/** Substitute for the firmware UART writer. */
void console_write(const char *text)
{
    size_t length = strlen(text);
    assert(length < sizeof output - used);
    memcpy(output + used, text, length + 1);
    used += length;
}

/** Discard the previous terminal capture. */
static void clear_output(void)
{
    used = 0;
    output[0] = '\0';
}

/** Deliver a NUL-terminated sequence of terminal bytes to the game. */
static void type_keys(const char *keys)
{
    while (*keys) {
        tetris_game.key(*keys++);
    }
}

/** Check idle callbacks, irrelevant terminal sequences and interrupted arrow keys. */
static void test_input_boundaries(void)
{
    assert(tetris_game.finished());
    tetris_game.tick(UINT32_MAX);
    type_keys("ADWSQ");
    tetris_game.draw();
    assert(used == 0);

    tetris_game.start(GAME_NORMAL);
    tetris_game.draw();
    clear_output();
    type_keys("\033[5~\033OP\033OQ\033[6n");
    tetris_game.draw();
    assert(used == 0 && !tetris_game.finished());
    type_keys("\033Q");
    assert(tetris_game.finished());
    assert(strcmp(output, RESET_STYLE) == 0);
    clear_output();
    tetris_game.tick(UINT32_MAX);
    tetris_game.draw();
    assert(used == 0);

    tetris_game.start(GAME_EASY);
    type_keys("\033[");
    tetris_game.tick(400);
    tetris_game.key('Q');
    assert(tetris_game.finished());

    tetris_game.start(GAME_EASY);
    type_keys("\033[");
    tetris_game.start(GAME_EASY);
    tetris_game.draw();
    clear_output();
    tetris_game.key('A');
    tetris_game.draw();
    assert(used != 0);
    clear_output();
    puts("PASS Game API: idle callbacks, escape sequences, and input reset");
}

/** Check each difficulty, the frame, key handling, and timed incremental output. */
static void test_game_callbacks(void)
{
    tetris_game.start(GAME_EASY);
    tetris_game.draw();
    assert(strstr(output, "\033[2J\033[H"));
    assert(strstr(output, "Difficulty: Easy"));
    assert(strstr(output, "Score 0 L1 Ln0") && strstr(output, "Drop: 850 ms"));
    assert(strstr(output, "A/D or Left/Right"));
    assert(strstr(output, "W/Up") && strstr(output, "S/Down"));
    assert(strstr(output, "\033[38;2;255;133;199m\033[48;2;24;15;39m\033[2J\033[HTETRIS"));
    assert(strstr(output, "\033[48;2;112;63;162m        "));
    assert(strstr(output, "\033[48;2;220;84;155m        "));
    assert(strstr(output, "\033[48;2;255;172;123m        "));
    assert(strstr(output, T_BG));
    assert(strstr(output, "Next block:"));
    assert(strstr(output, I_BG "[][][][]"));

    clear_output();
    tetris_game.tick(849);
    tetris_game.key('\r');
    tetris_game.draw();
    assert(used == 0);
    tetris_game.tick(1);
    tetris_game.draw();
    assert(used != 0 && !strstr(output, "\033[2J"));
    clear_output();
    tetris_game.key('A');
    tetris_game.draw();
    assert(used != 0 && !strstr(output, "\033[2J"));
    clear_output();
    tetris_game.key('d');
    tetris_game.draw();
    assert(used != 0);
    clear_output();
    tetris_game.key('W');
    tetris_game.draw();
    assert(used != 0);
    clear_output();
    tetris_game.key('S');
    tetris_game.draw();
    assert(used != 0);
    tetris_game.key('q');
    assert(tetris_game.finished());

    clear_output();
    tetris_game.start(GAME_NORMAL);
    tetris_game.draw();
    assert(!tetris_game.finished());
    assert(strstr(output, "Difficulty: Normal") && strstr(output, "600 ms"));
    clear_output();
    tetris_game.start(GAME_HARD);
    tetris_game.draw();
    assert(strstr(output, "Difficulty: Hard") && strstr(output, "350 ms"));
    puts("PASS Game API: three difficulties, keys and incremental color rendering");
}

/** Exercise a complete loss with key input, then check both return paths and restart. */
static void test_result_and_restart(void)
{
    char initial[sizeof output];
    clear_output();
    tetris_game.start(GAME_NORMAL);
    tetris_game.draw();
    memcpy(initial, output, used + 1);
    bool result_shown = false;
    for (int step = 0; step < 128; ++step) {
        clear_output();
        tetris_game.key('s');
        tetris_game.draw();
        if (strstr(output, "\033[2KGame over.")) {
            assert(strstr(output, "\033[38;2;255;133;199m"));
            result_shown = true;
            break;
        }
    }
    assert(result_shown && !tetris_game.finished());
    clear_output();
    tetris_game.key('w');
    tetris_game.tick(5000);
    tetris_game.draw();
    assert(used == 0 && !tetris_game.finished());
    tetris_game.key('Q');
    assert(tetris_game.finished());
    assert(strcmp(output, RESET_STYLE) == 0);

    clear_output();
    tetris_game.start(GAME_NORMAL);
    tetris_game.draw();
    assert(!tetris_game.finished() && strcmp(output, initial) == 0);
    tetris_game.key('q');
    assert(tetris_game.finished());
    puts("PASS Game API: game over waits for Q and restart redraws the same clean frame");
}

/** Replay five line clears using only public Game callbacks. */
static void test_level_replay(void)
{
    const struct {
        const char *keys;
        int drops;
    } moves[] = {
        {"", 17},      {"dddd", 17},  {"wwddd", 16},  {"aaaaa", 17},
        {"aa", 17},    {"ddddd", 17}, {"wwaaaa", 16}, {"wwdd", 16},
        {"wwddddd", 16}, {"ddddd", 15}, {"ddd", 16},    {"a", 17},
        {"", 17},      {"aaaa", 17},  {"dd", 17},     {"aaaa", 17}
    };

    clear_output();
    tetris_game.start(GAME_NORMAL);
    tetris_game.draw();
    for (size_t piece = 0; piece < sizeof moves / sizeof moves[0]; ++piece) {
        clear_output();
        for (const char *key = moves[piece].keys; *key; ++key) {
            tetris_game.key(*key);
            if (*key == 'w') {
                tetris_game.tick(180);
            }
        }
        for (int drop = 0; drop < moves[piece].drops; ++drop) {
            tetris_game.key('s');
        }
        tetris_game.draw();
        assert(!tetris_game.finished() && !strstr(output, "Game over."));
        if (piece == 4) {
            assert(strstr(output, "Score 100 L1 Ln1") && strstr(output, "Drop: 600 ms"));
        }
    }
    assert(strstr(output, "Score 500 L2 Ln5") && strstr(output, "Drop: 530 ms"));
    assert(!strstr(output, "\033[2J"));
    clear_output();
    tetris_game.tick(529);
    tetris_game.draw();
    assert(used == 0);
    tetris_game.tick(1);
    tetris_game.draw();
    assert(used != 0);
    puts("PASS Game API: five lines by key input, score 500, Normal level 2 at 530 ms");
}

/** Compare cursor updates for the four arrow keys and the matching letters. */
static void test_arrow_keys(void)
{
    const struct {
        char letter;
        const char *arrow;
    } keys[] = {
        {'A', "\033[D"}, {'D', "\033OC"}, {'W', "\033[1;5A"}, {'S', "\033[B"}
    };
    char letter_frame[sizeof output];
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
        tetris_game.start(GAME_NORMAL);
        clear_output();
        tetris_game.draw();
        clear_output();
        tetris_game.key(keys[i].letter);
        tetris_game.draw();
        assert(used != 0);
        memcpy(letter_frame, output, used + 1);

        tetris_game.start(GAME_NORMAL);
        clear_output();
        tetris_game.draw();
        clear_output();
        type_keys(keys[i].arrow);
        tetris_game.draw();
        assert(strcmp(output, letter_frame) == 0);
    }
    puts("PASS Game API: CSI and SS3 arrow sequences match WASD");
}

/** Flood the game with W and check the turn rate and the first gravity step. */
static void test_rotation_repeat(void)
{
    tetris_game.start(GAME_EASY);
    clear_output();
    tetris_game.draw();
    unsigned rotations = 0;
    for (unsigned ms = 0; ms < 849; ++ms) {
        clear_output();
        tetris_game.key('W');
        tetris_game.draw();
        rotations += used != 0;
        clear_output();
        tetris_game.tick(1);
        tetris_game.draw();
        assert(used == 0);
    }
    assert(rotations == 5);
    tetris_game.tick(1);
    tetris_game.draw();
    assert(used != 0);
    puts("PASS Game API: repeated W turns at most every 180 ms; gravity drops at 850 ms");
}

/** Confirm that the first preview stays put during a fall and updates on spawn. */
static void test_preview_update(void)
{
    tetris_game.start(GAME_EASY);
    clear_output();
    tetris_game.draw();
    clear_output();
    for (int step = 0; step < 16; ++step) {
        tetris_game.key('S');
        tetris_game.draw();
        assert(!strstr(output, "\033[6;30H"));
        clear_output();
    }
    tetris_game.key('S');
    tetris_game.draw();
    assert(strstr(output, "\033[6;30H") && strstr(output, I_BG "[][][][]"));
    puts("PASS Game API: next piece updates when the earlier preview spawns");
}

/** Check that Space places the active shape on top of its visible ghost. */
static void test_ghost_and_space(void)
{
    tetris_game.start(GAME_EASY);
    clear_output();
    tetris_game.draw();
    assert(strstr(output, "Space"));
    assert(strstr(output, "\033[21;10H......"));
    assert(strstr(output, "\033[38;2;205;170;209m"));
    clear_output();
    tetris_game.key(' ');
    tetris_game.draw();
    assert(strstr(output, "\033[21;10H[][][]"));
    assert(strstr(output, T_BG));
    assert(strstr(output, "\033[6;30H"));
    assert(!strstr(output, "4 LINES"));
    puts("PASS Game API: Space locks on the ghost and refreshes the preview");
}

/** Check the background of the cells vacated by the active figure and its ghost. */
static void test_move_restores_background(void)
{
    tetris_game.start(GAME_EASY);
    clear_output();
    tetris_game.draw();
    clear_output();
    tetris_game.key('A');
    tetris_game.draw();
    assert(strstr(output, "\033[5;14H" FIELD_BG "  "));
    assert(strstr(output, "\033[21;14H  "));
    assert(!strstr(output, "\033[2J"));
    clear_output();
    tetris_game.key('q');
    assert(strcmp(output, RESET_STYLE) == 0);
    puts("PASS retrowave: incremental move restores field background and Q resets ANSI");
}

/** Run all Game callback checks. */
int main(void)
{
    test_input_boundaries();
    test_game_callbacks();
    test_result_and_restart();
    test_arrow_keys();
    test_rotation_repeat();
    test_preview_update();
    test_ghost_and_space();
    test_move_restores_background();
    test_level_replay();
    return 0;
}
