/** @file tetris_game_test.c
 *  Game callback checks with a host-side USART2 substitute.
 */
#include "tetris.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

/** Check that callbacks are safe while idle and that escape sequences are ignored. */
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
    type_keys("\033[A\033[D\033[1;5D\033OA\033OD\033[5~");
    tetris_game.draw();
    assert(used == 0 && !tetris_game.finished());
    type_keys("\033Q");
    assert(tetris_game.finished());
    tetris_game.tick(UINT32_MAX);
    tetris_game.draw();
    assert(used == 0);

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
    assert(strstr(output, "Score: 0 | Level: 1 | Lines: 0 | Drop: 850 ms"));
    assert(strstr(output, "A/D Move | W Rotate | S Drop one row | Q Menu"));
    assert(strstr(output, "\033[44m") || strstr(output, "\033[45m") ||
           strstr(output, "\033[46m") || strstr(output, "\033[43m") ||
           strstr(output, "\033[42m") || strstr(output, "\033[41m") ||
           strstr(output, "\033[47m"));

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
        if (strstr(output, "\033[31mGame over.\033[0m  Q - menu")) {
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
        {"aaaa", 17},     {"a", 17},      {"wwaaaa", 15},  {"dddddd", 17},
        {"wwd", 16},      {"ddd", 16},    {"aa", 16},      {"wdddd", 16},
        {"wwdddddd", 16}, {"dd", 17},     {"www", 16},     {"aaaa", 17},
        {"ddd", 16},      {"waa", 16},    {"wwwaaa", 16},  {"wa", 16},
        {"dddddd", 17}
    };

    clear_output();
    tetris_game.start(GAME_NORMAL);
    tetris_game.draw();
    for (size_t piece = 0; piece < sizeof moves / sizeof moves[0]; ++piece) {
        clear_output();
        type_keys(moves[piece].keys);
        for (int drop = 0; drop < moves[piece].drops; ++drop) {
            tetris_game.key('s');
        }
        tetris_game.draw();
        assert(!tetris_game.finished() && !strstr(output, "Game over."));
        if (piece == 6) {
            assert(strstr(output, "Score: 100 | Level: 1 | Lines: 1 | Drop: 600 ms"));
        }
    }
    assert(strstr(output, "Score: 500 | Level: 2 | Lines: 5 | Drop: 530 ms"));
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

/** Run all Game callback checks. */
int main(void)
{
    test_input_boundaries();
    test_game_callbacks();
    test_result_and_restart();
    test_level_replay();
    return 0;
}
