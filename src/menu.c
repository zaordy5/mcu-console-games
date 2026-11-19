#include "game.h"
#include "platform.h"

/** Placeholder state shared by the three previews until game modules arrive. */
static bool done;
/** Reset the preview session. */
static void preview_start(GameDifficulty difficulty) { (void)difficulty; done = false; }
/** Leave the preview on q. */
static void preview_key(char input) { done = (input == 'q'); }
/** Advance an inert preview by the supplied interval. */
static void preview_tick(uint32_t elapsed_ms) { (void)elapsed_ms; }
/** The preview screen is static after selection. */
static void preview_draw(void) { }
/** Report a requested return to the menu. */
static bool preview_finished(void) { return done; }
/** Stage-one entries. Replace individual entries with each game's callbacks. */
static const Game games[] = {
    {"Tetris", preview_start, preview_key, preview_tick, preview_draw, preview_finished},
    {"Minesweeper", preview_start, preview_key, preview_tick, preview_draw, preview_finished},
    {"Pong", preview_start, preview_key, preview_tick, preview_draw, preview_finished}
};

/** Clear the screen, return the cursor home and display all registered games. */
static void menu(void)
{
    console_write("\033[2J\033[H\033[36mMCU Console Games\033[0m\r\n");
    for (unsigned i = 0; i < sizeof games / sizeof games[0]; ++i) {
        char number[] = {(char)('1' + i), '.', ' ', 0};
        console_write(number);
        console_write(games[i].name);
        console_write("\r\n");
    }
}

/** Dispatch input and elapsed time to the selected module. */
int main(void)
{
    platform_init();
    const Game *active = 0;
    uint32_t previous = platform_ms();
    menu();
    for (;;) {
        int key = console_read();
        uint32_t now = platform_ms();
        if (!active) {
            if (key >= '1' && key <= '3') {
                active = &games[key - '1'];
                active->start(GAME_NORMAL);
                console_write("\033[2J\033[H\033[33m");
                console_write(active->name);
                console_write(" preview\033[0m\r\nq - menu\r\n");
                active->draw();
            }
        } else {
            if (key >= 0) active->key((char)key);
            if (now != previous) active->tick(now - previous);
            if (active->finished()) {
                active = 0;
                menu();
            } else if (key >= 0 || now != previous) {
                active->draw();
            }
        }
        previous = now;
    }
}
