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

/** Check both serve hints and restoration of the center mark under the ball. */
static void test_serve_marker(void)
{
    pong_game.start(GAME_EASY);
    draw_frame();
    assert(strstr(output, "Serve -> CPU"));
    pong_game.tick(2771);
    draw_frame();
    assert(!strstr(output, "YOUR POINT"));
    pong_game.tick(1);
    draw_frame();
    assert(strstr(output, "\033[1;36m1\033[0m : \033[1;35m0") && strstr(output, "Serve <- YOU"));
    assert(strstr(output, "YOUR POINT"));
    assert(strstr(output, "\033[12;22H\033[1;97mO"));
    pong_game.tick(828);
    draw_frame();
    assert(strstr(output, "\033[12;22H\033[90m."));
    assert(!strstr(output, "YOUR POINT"));
    pong_game.tick(2052);
    draw_frame();
    assert(strstr(output, "CPU POINT") && strstr(output, "Serve -> CPU"));
}

/** Check colors, incremental redraw, match result, Q and session reset. */
int main(void)
{
    pong_game.start(GAME_EASY);
    draw_frame();
    assert(strstr(output, "\033[1;36m0\033[0m : \033[1;35m0") && strstr(output, "L1  Easy"));
    assert(strstr(output, "\033[01;13H\033[2K\033[1;36mYOU"));
    assert(strstr(output, "First to 5") && strstr(output, "W/S  Move"));
    assert(strstr(output, "\033[90m+"));
    assert(strstr(output, "|\033[20C.\033[19C|\r\n|\033[40C|\r\n"));
    assert(written < 760);
    printf("Pong first frame: %zu UART bytes\n", written);
    assert(strstr(output, "\033[36;106m|") && strstr(output, "\033[35;105m|"));
    assert(strstr(output, "\033[1;97mO") && strstr(output, "\033[1mPONG"));

    draw_frame();
    assert(written == 0);
    pong_game.key('W');
    draw_frame();
    assert(strstr(output, "\033[08;02H\033[36;106m|"));
    assert(!strstr(output, "\033[2J"));
    pong_game.key('s');
    draw_frame();
    assert(strstr(output, "\033[13;02H\033[36;106m|"));
    pong_game.tick(27000);
    draw_frame();
    assert(strstr(output, "YOU WIN!") && strstr(output, "N  Next level"));
    assert(strstr(output, "Final \033[1;36m5\033[0m : \033[1;35m4"));
    assert(!pong_game.finished());
    pong_game.key('n');
    draw_frame();
    assert(strstr(output, "\033[1;36m0\033[0m : \033[1;35m0") && strstr(output, "L2  Easy"));
    assert(strstr(output, "Serve -> CPU") && strstr(output, "W/S  Move"));
    pong_game.key('q');
    assert(pong_game.finished());

    pong_game.start(GAME_HARD);
    assert(!pong_game.finished());
    draw_frame();
    assert(strstr(output, "L1  Hard"));
    for (unsigned key = 0; key < 16; ++key) {
        pong_game.key('w');
    }
    pong_game.tick(72000);
    draw_frame();
    assert(strstr(output, "CPU WINS!") && strstr(output, "Q  Menu"));
    assert(!strstr(output, "N  Next level") && !strstr(output, "W/S  Move"));
    assert(!pong_game.finished());
    pong_game.key('N');
    draw_frame();
    assert(written == 0);
    pong_game.key('Q');
    assert(pong_game.finished());

    pong_game.start(GAME_NORMAL);
    draw_frame();
    assert(strstr(output, "\033[1;36m0\033[0m : \033[1;35m0") && strstr(output, "L1  Normal"));
    assert(strstr(output, "Serve -> CPU") && !pong_game.finished());
    test_serve_marker();
    puts("Pong Game callbacks and ANSI checks passed");
}
