/** @file menu.c
 *  Retrowave launcher and shared USART2 game dispatch.
 */
#include "game.h"
#include "minesweeper.h"
#include "platform.h"
#include "pong.h"
#include "tetris.h"

/** Clear all terminal attributes before switching screens. */
#define RESET "\033[0m"
/** Background for the full launcher. */
#define BASE "\033[48;2;24;15;39m"
/** Background for game and difficulty cards. */
#define PANEL "\033[48;2;36;22;56m"
/** Color for the launcher title. */
#define PINK "\033[38;2;255;133;199m"
/** Color for labels and descriptions. */
#define INK "\033[38;2;255;238;250m"
/** Color for key numbers and footer instructions. */
#define PEACH "\033[38;2;255;203;151m"

/** Games in menu order, addressed by the keys 1–3. */
static const Game *const games[] = {&tetris_game, &minesweeper_game, &pong_game};

/** The rule affected by difficulty in each game. */
static const char *const difficulty_rules[] = {
    "Difficulty changes the fall speed.",
    "Difficulty changes the mine density.",
    "Difficulty changes the opponent's reaction time.",
};

/** Labels for the three GameDifficulty values. */
static const char *const difficulties[] = {"Easy", "Normal", "Hard"};

/** Three-row ASCII key glyphs for the numbered cards. */
static const char *const key_glyphs[][3] = {
    {" /| ", "  | ", " _|_"},
    {" == ", "  / ", " /__"},
    {" == ", "  _)", " __)"},
};

/** Position the cursor in the 80 x 24 launcher viewport. */
static void move_to(unsigned row, unsigned column)
{
    char position[] = {
        '\033', '[', (char)('0' + row / 10), (char)('0' + row % 10),
        ';', (char)('0' + column / 10), (char)('0' + column % 10), 'H', '\0'
    };
    console_write(position);
}

/** Draw the common title and the three sections of the sunset line. */
static void header(const char *subtitle)
{
    console_write(RESET BASE "\033[2J\033[H");
    move_to(2, 15);
    console_write(PINK "MCU CONSOLE GAMES");
    move_to(3, 15);
    console_write("\033[48;2;112;63;162m                  "
                  "\033[48;2;220;84;155m                 "
                  "\033[48;2;255;172;123m                 " BASE);
    move_to(5, 15);
    console_write(INK);
    console_write(subtitle);
}

/** Paint a card with a large key glyph and a single readable label. */
static void card(unsigned top, unsigned key, const char *label)
{
    for (unsigned line = 0; line < 3; ++line) {
        move_to(top + line, 15);
        console_write(PANEL "                                                    ");
        move_to(top + line, 18);
        console_write(PEACH);
        console_write(key_glyphs[key][line]);
    }
    move_to(top + 1, 26);
    console_write(INK);
    console_write(label);
}

/** Display the main menu. Every return uses the same bytes as startup. */
static void show_menu(void)
{
    header("Choose a game");
    for (unsigned i = 0; i < sizeof games / sizeof games[0]; ++i) {
        card(8 + 4 * i, i, games[i]->name);
    }
    move_to(21, 15);
    console_write(BASE PEACH "1-3 Select game" RESET);
}

/** Show the difficulty choices and their effect for the selected game. */
static void show_difficulty(unsigned game)
{
    header("Choose difficulty");
    move_to(6, 15);
    console_write(PINK);
    console_write(games[game]->name);
    for (unsigned i = 0; i < sizeof difficulties / sizeof difficulties[0]; ++i) {
        card(8 + 4 * i, i, difficulties[i]);
    }
    move_to(20, 15);
    console_write(BASE INK);
    console_write(difficulty_rules[game]);
    move_to(22, 15);
    console_write(PEACH "1-3 Start   |   Q Back" RESET);
}

/** Dispatch nonblocking input and elapsed time to the active game. */
int main(void)
{
    platform_init();
    const Game *active = 0;
    int pending = -1;
    uint32_t previous = platform_ms();
    show_menu();
    for (;;) {
        int key = console_read();
        uint32_t now = platform_ms();
        if (active) {
            if (key >= 0) active->key((char)key);
            if (now != previous) active->tick(now - previous);
            if (active->finished()) {
                active = 0;
                show_menu();
            } else if (key >= 0 || now != previous) {
                active->draw();
            }
        } else if (pending >= 0) {
            if (key == 'q' || key == 'Q') {
                pending = -1;
                show_menu();
            } else if (key >= '1' && key <= '3') {
                active = games[pending];
                pending = -1;
                console_write(RESET "\033[2J\033[H");
                active->start((GameDifficulty)(key - '1'));
                active->draw();
                previous = platform_ms();
                continue;
            }
        } else if (key >= '1' && key <= '3') {
            pending = key - '1';
            show_difficulty((unsigned)pending);
        }
        previous = now;
    }
}
