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
    pong_game.tick(2309);
    draw_frame();
    assert(!strstr(output, "YOUR POINT"));
    pong_game.tick(1);
    draw_frame();
    assert(strstr(output, "1\033[38;2;255;238;250m : \033[38;2;217;190;255m0") && strstr(output, "Serve <- YOU"));
    assert(strstr(output, "YOUR POINT"));
    assert(strstr(output, "\033[12;22H\033[38;2;255;238;250mO"));
    pong_game.tick(690);
    draw_frame();
    assert(strstr(output, "\033[12;22H\033[38;2;149;117;173m\033[48;2;36;22;56m."));
    assert(!strstr(output, "YOUR POINT"));
    pong_game.tick(1710);
    draw_frame();
    assert(strstr(output, "CPU POINT") && strstr(output, "Serve -> CPU"));
}

/** Check colors, incremental redraw, match result, Q and session reset. */
int main(void)
{
    pong_game.start(GAME_EASY);
    draw_frame();
    assert(strstr(output, "0\033[38;2;255;238;250m : \033[38;2;217;190;255m0") && strstr(output, "L1  Easy"));
    assert(strstr(output, "\033[01;13H\033[38;2;255;119;191m\033[2KYOU"));
    assert(strstr(output, "First to 5") && strstr(output, "W/S  Move"));
    assert(strstr(output, "\033[48;2;24;15;39m\033[2J"));
    assert(strstr(output, "\033[48;2;36;22;56m                    ."));
    assert(strstr(output, "\033[48;2;112;63;162m   \033[48;2;220;84;155m   \033[48;2;255;172;123m   "));
    assert(written < sizeof output);
    printf("Pong first frame: %zu UART bytes\n", written);
    assert(strstr(output, "\033[48;2;255;119;191m ") && strstr(output, "\033[48;2;217;190;255m "));
    assert(strstr(output, "\033[48;2;36;22;56mO"));
    assert(strstr(output, "\033[38;2;255;133;199m\033[48;2;24;15;39m\033[2KPONG"));

    draw_frame();
    assert(written == 0);
    pong_game.key('W');
    draw_frame();
    assert(strstr(output, "\033[08;02H\033[38;2;255;238;250m\033[48;2;255;119;191m "));
    assert(!strstr(output, "\033[2J"));
    assert(strstr(output, "\033[13;02H\033[38;2;149;117;173m\033[48;2;36;22;56m "));
    pong_game.key('s');
    draw_frame();
    assert(strstr(output, "\033[13;02H\033[38;2;255;238;250m\033[48;2;255;119;191m "));
    pong_game.tick(22000);
    draw_frame();
    assert(strstr(output, "YOU WIN!") && strstr(output, "N  Next level"));
    assert(strstr(output, "Final \033[38;2;255;119;191m5\033[38;2;255;238;250m : \033[38;2;217;190;255m4"));
    assert(!pong_game.finished());
    pong_game.key('n');
    draw_frame();
    assert(strstr(output, "0\033[38;2;255;238;250m : \033[38;2;217;190;255m0") && strstr(output, "L2  Easy"));
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
    pong_game.tick(60000);
    draw_frame();
    assert(strstr(output, "CPU WINS!") && strstr(output, "Q  Menu"));
    assert(!strstr(output, "N  Next level") && !strstr(output, "W/S  Move"));
    assert(!pong_game.finished());
    pong_game.key('N');
    draw_frame();
    assert(written == 0);
    pong_game.key('Q');
    assert(pong_game.finished() && strcmp(output, "\033[0m") == 0);

    pong_game.start(GAME_NORMAL);
    draw_frame();
    assert(strstr(output, "0\033[38;2;255;238;250m : \033[38;2;217;190;255m0") && strstr(output, "L1  Normal"));
    assert(strstr(output, "Serve -> CPU") && !pong_game.finished());
    test_serve_marker();
    puts("Pong Game callbacks and ANSI checks passed");
}
