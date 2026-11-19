/** @file tetris_game_test.c
 *  Game callback checks with a host-side USART2 substitute.
 */
#include "tetris.h"

#include <assert.h>
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

/** Run all Game callback checks. */
int main(void)
{
    test_game_callbacks();
    test_result_and_restart();
    return 0;
}
