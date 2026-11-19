/** @file pong_game_test.c
 *  Host regression checks for the shared callbacks and ANSI output.
 */
#include "pong.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/** Capture space for all writes during a frame. */
static char output[2048];
/** Number of captured bytes in the current frame. */
static size_t written;

/** Capture each ANSI fragment emitted by the production Game adapter. */
void console_write(const char *text)
{
    size_t length = strlen(text);
    assert(length < sizeof output - written);
    memcpy(output + written, text, length + 1);
    written += length;
}

/** Reset the capture and draw one frame. */
static void draw_frame(void)
{
    written = 0;
    output[0] = '\0';
    pong_game.draw();
}

/** Check colors, incremental redraw, match result, Q and session reset. */
int main(void)
{
    pong_game.start(GAME_EASY);
    draw_frame();
    assert(strstr(output, "You 0:0 CPU") && strstr(output, "L1  Easy"));
    assert(strstr(output, "W/S - move  Q - menu"));
    assert(strstr(output, "\033[1;36m|") && strstr(output, "\033[1;35m|"));
    assert(strstr(output, "\033[1;32mO") && strstr(output, "\033[1;33mPong"));

    draw_frame();
    assert(written == 0);
    pong_game.key('W');
    draw_frame();
    assert(strstr(output, "\033[08;02H\033[1;36m|"));
    assert(!strstr(output, "\033[2J"));
    pong_game.key('s');
    draw_frame();
    assert(strstr(output, "\033[13;02H\033[1;36m|"));
    pong_game.tick(22000);
    draw_frame();
    assert(strstr(output, "You win!") && strstr(output, "N - next level"));
    assert(!pong_game.finished());
    pong_game.key('n');
    draw_frame();
    assert(strstr(output, "You 0:0 CPU") && strstr(output, "L2  Easy"));
    assert(strstr(output, "Serving..."));
    pong_game.key('q');
    assert(pong_game.finished());

    pong_game.start(GAME_HARD);
    assert(!pong_game.finished());
    draw_frame();
    assert(strstr(output, "L1  Hard"));
    for (unsigned key = 0; key < 16; ++key) {
        pong_game.key('w');
    }
    pong_game.tick(60000);
    draw_frame();
    assert(strstr(output, "CPU wins!"));
    assert(!pong_game.finished());
    pong_game.key('N');
    draw_frame();
    assert(written == 0);
    pong_game.key('Q');
    assert(pong_game.finished());

    pong_game.start(GAME_NORMAL);
    draw_frame();
    assert(strstr(output, "You 0:0 CPU") && strstr(output, "L1  Normal"));
    assert(strstr(output, "Serving...") && !pong_game.finished());
    puts("Pong Game callbacks and ANSI checks passed");
}
