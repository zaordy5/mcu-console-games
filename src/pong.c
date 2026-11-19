/** @file pong.c
 *  Shared Game callbacks and ANSI terminal rendering for Pong.
 */
#include "pong.h"
#include "pong_core.h"

/** Every playable cell is offset by the screen header and upper border. */
#define FIELD_ROW 4
/** First terminal column for playable cells, after the side border. */
#define FIELD_COLUMN 2
/** Terminal row for controls and match results. */
#define STATUS_ROW (FIELD_ROW + PONG_HEIGHT + 1)

/** Current session controlled by the Game callbacks. */
static Pong match;
/** Last rendered state used to update only changed cells. */
static Pong drawn;
/** Whether the next draw must paint the static field and all pieces. */
static bool fresh_screen;
/** Whether the user requested a return to the menu. */
static bool exit_requested;

/** Move the ANSI cursor to a 1-based position with two decimal digits per axis. */
static void cursor_at(unsigned row, unsigned column)
{
    char code[] = {
        '\033', '[', (char)('0' + row / 10), (char)('0' + row % 10),
        ';', (char)('0' + column / 10), (char)('0' + column % 10), 'H', '\0'
    };
    console_write(code);
}

/** Write one or two decimal digits for a score or level. */
static void write_number(unsigned value)
{
    char digits[] = { (char)('0' + value / 10), (char)('0' + value % 10), '\0' };
    console_write(digits + (value < 10));
}

/** Paint the score line with both scores, current level and selected difficulty. */
static void draw_score(void)
{
    cursor_at(1, 1);
    console_write("\033[2K\033[1;33mPong  You ");
    write_number(match.player_score);
    console_write(":");
    write_number(match.cpu_score);
    console_write(" CPU\033[0m  L");
    write_number(match.level);
    console_write("  ");
    switch (match.difficulty) {
    case PONG_EASY: console_write("Easy"); break;
    case PONG_HARD: console_write("Hard"); break;
    default: console_write("Normal"); break;
    }
}

/** Paint a horizontal border or a blank row between the side borders. */
static void draw_field_row(bool border)
{
    char row[PONG_WIDTH + 5];
    row[0] = border ? '+' : '|';
    for (unsigned x = 1; x <= PONG_WIDTH; ++x) {
        row[x] = border ? '-' : ' ';
    }
    row[PONG_WIDTH + 1] = row[0];
    row[PONG_WIDTH + 2] = '\r';
    row[PONG_WIDTH + 3] = '\n';
    row[PONG_WIDTH + 4] = '\0';
    console_write(row);
}

/** Clear the screen and draw the fixed court and help text. */
static void draw_field(void)
{
    console_write("\033[0m\033[2J\033[H\r\nW/S - move  Q - menu\r\n");
    draw_field_row(true);
    for (unsigned y = 0; y < PONG_HEIGHT; ++y) {
        draw_field_row(false);
    }
    draw_field_row(true);
}

/** Show serve, controls after victory or the final loss. */
static void draw_status(void)
{
    cursor_at(STATUS_ROW, 1);
    console_write("\033[2K");
    switch (match.phase) {
    case PONG_SERVE:
        console_write("Serving...");
        break;
    case PONG_WON:
        if (match.level < PONG_MAX_LEVEL) {
            console_write("\033[1;32mYou win!\033[0m  N - next level  Q - menu");
        } else {
            console_write("\033[1;32mYou win!\033[0m  Final level  Q - menu");
        }
        break;
    case PONG_LOST:
        console_write("\033[1;31mCPU wins!\033[0m  Q - menu");
        break;
    case PONG_RALLY:
        break;
    }
}

/** Update a paddle, erasing only the cells that its movement left empty. */
static void draw_paddle(unsigned column, int top, int old_top, const char *color)
{
    for (int row = old_top; !fresh_screen && row < old_top + PONG_PADDLE_SIZE; ++row) {
        if (row < top || row >= top + PONG_PADDLE_SIZE) {
            cursor_at(FIELD_ROW + row, column);
            console_write(" ");
        }
    }
    for (int row = top; row < top + PONG_PADDLE_SIZE; ++row) {
        if (fresh_screen || row < old_top || row >= old_top + PONG_PADDLE_SIZE) {
            cursor_at(FIELD_ROW + row, column);
            console_write(color);
        }
    }
}

/** Reset the match and translate the shared difficulty values explicitly. */
static void start(GameDifficulty difficulty)
{
    PongDifficulty setting = PONG_NORMAL;
    if (difficulty == GAME_EASY) {
        setting = PONG_EASY;
    } else if (difficulty == GAME_HARD) {
        setting = PONG_HARD;
    }
    pong_start(&match, setting);
    exit_requested = false;
    fresh_screen = true;
}

/** Handle either case of the game's controls. */
static void key(char input)
{
    switch (input) {
    case 'w': case 'W': pong_move(&match, -1); break;
    case 's': case 'S': pong_move(&match, 1); break;
    case 'n': case 'N': pong_next_level(&match); break;
    case 'q': case 'Q': exit_requested = true; break;
    }
}

/** Advance the match with the elapsed time supplied by the platform. */
static void tick(uint32_t elapsed_ms)
{
    if (!exit_requested) {
        pong_tick(&match, elapsed_ms);
    }
}

/** Paint the first frame and subsequent changes using only console_write. */
static void draw(void)
{
    if (fresh_screen) {
        draw_field();
    }
    if (fresh_screen || drawn.player_score != match.player_score ||
        drawn.cpu_score != match.cpu_score || drawn.level != match.level) {
        draw_score();
    }
    draw_paddle(FIELD_COLUMN, match.player_y, drawn.player_y, "\033[1;36m|\033[0m");
    draw_paddle(FIELD_COLUMN + PONG_WIDTH - 1, match.cpu_y, drawn.cpu_y, "\033[1;35m|\033[0m");

    if (fresh_screen || drawn.ball_x != match.ball_x || drawn.ball_y != match.ball_y) {
        if (!fresh_screen) {
            cursor_at(FIELD_ROW + drawn.ball_y, FIELD_COLUMN + drawn.ball_x);
            console_write(" ");
        }
        cursor_at(FIELD_ROW + match.ball_y, FIELD_COLUMN + match.ball_x);
        console_write("\033[1;32mO\033[0m");
    }
    if (fresh_screen || drawn.phase != match.phase || drawn.level != match.level) {
        draw_status();
    }
    drawn = match;
    fresh_screen = false;
}

/** Report completion of the session once Q has been pressed. */
static bool finished(void)
{
    return exit_requested;
}

/** Public descriptor used for the Pong entry in the shared menu. */
const Game pong_game = {
    .name = "Pong",
    .start = start,
    .key = key,
    .tick = tick,
    .draw = draw,
    .finished = finished,
};
