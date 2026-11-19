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
/** Terminal row for the active keys. */
#define CONTROLS_ROW (STATUS_ROW + 1)
/** Left edge of the centered 17-cell scoreboard. */
#define SCORE_COLUMN ((PONG_WIDTH + 2 - 17) / 2 + 1)
/** ANSI RGB roles from the Minesweeper retro palette. */
typedef enum {
    INK, PINK, PEACH, PLAYER, CPU, BASE, FIELD, COURT, SUNSET_VIOLET,
    SUNSET_PINK, SUNSET_PEACH, NO_COLOR
} Color;

/** Decimal RGB channels for ANSI SGR. */
static const char *const rgb[] = {
    [INK] = "255;238;250", [PINK] = "255;133;199", [PEACH] = "255;203;151",
    [PLAYER] = "255;119;191", [CPU] = "217;190;255", [BASE] = "24;15;39",
    [FIELD] = "36;22;56", [COURT] = "149;117;173", [SUNSET_VIOLET] = "112;63;162",
    [SUNSET_PINK] = "220;84;155", [SUNSET_PEACH] = "255;172;123"
};

/** Last ANSI foreground color written to the terminal. */
static Color foreground = NO_COLOR;
/** Last ANSI background color written to the terminal. */
static Color background = NO_COLOR;

/** Current session controlled by the Game callbacks. */
static Pong match;
/** Last rendered state used to update only changed cells. */
static Pong drawn;
/** Whether the next draw must paint the static field and all pieces. */
static bool fresh_screen;
/** Whether the user requested a return to the menu. */
static bool exit_requested;

/** Emit only RGB channels that changed. */
static void style(Color fg, Color bg)
{
    if (fg != foreground) {
        console_write("\033[38;2;");
        console_write(rgb[fg]);
        console_write("m");
        foreground = fg;
    }
    if (bg != background) {
        console_write("\033[48;2;");
        console_write(rgb[bg]);
        console_write("m");
        background = bg;
    }
}

/** Return the terminal to the shared menu's default style. */
static void reset_style(void)
{
    console_write("\033[0m");
    foreground = NO_COLOR;
    background = NO_COLOR;
}

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

/** Write the score with the same accents as the two paddles. */
static void write_score(void)
{
    style(PLAYER, BASE);
    write_number(match.player_score);
    style(INK, BASE);
    console_write(" : ");
    style(CPU, BASE);
    write_number(match.cpu_score);
}

/** Paint the centered score and the sunset strip to its right. */
static void draw_score(void)
{
    cursor_at(1, SCORE_COLUMN);
    style(PLAYER, BASE);
    console_write("\033[2KYOU   ");
    write_score();
    console_write("   CPU");
    cursor_at(1, PONG_WIDTH - 7);
    style(INK, SUNSET_VIOLET);
    console_write("   ");
    style(INK, SUNSET_PINK);
    console_write("   ");
    style(INK, SUNSET_PEACH);
    console_write("   ");
}

/** Paint the current level, difficulty and target score. */
static void draw_settings(void)
{
    cursor_at(2, 3);
    style(PINK, BASE);
    console_write("\033[2KPONG");
    style(INK, BASE);
    console_write("  |  L");
    write_number(match.level);
    console_write("  ");
    switch (match.difficulty) {
    case PONG_EASY: console_write("Easy"); break;
    case PONG_HARD: console_write("Hard"); break;
    default: console_write("Normal"); break;
    }
    style(PEACH, BASE);
    console_write("  |  First to ");
    write_number(PONG_WIN_SCORE);
}

/** Return the court glyph that belongs under an empty playable cell. */
static char court_tile(int x, int y)
{
    return x == PONG_WIDTH / 2 && y % 2 == 0 ? '.' : ' ';
}

/** Paint the top or bottom border of the court. */
static void draw_border(void)
{
    char row[PONG_WIDTH + 5];
    row[0] = '+';
    for (unsigned x = 1; x <= PONG_WIDTH; ++x) {
        row[x] = '-';
    }
    row[PONG_WIDTH + 1] = '+';
    row[PONG_WIDTH + 2] = '\r';
    row[PONG_WIDTH + 3] = '\n';
    row[PONG_WIDTH + 4] = '\0';
    console_write(row);
}

/** Clear the screen and draw the fixed court and help text. */
static void draw_field(void)
{
    reset_style();
    style(COURT, BASE);
    console_write("\033[2J\033[H\r\n\r\n");
    draw_border();
    for (int y = 0; y < PONG_HEIGHT; ++y) {
        char row[PONG_WIDTH + 1];
        for (int x = 0; x < PONG_WIDTH; ++x) {
            row[x] = court_tile(x, y);
        }
        row[PONG_WIDTH] = '\0';
        console_write("|");
        style(COURT, FIELD);
        console_write(row);
        style(COURT, BASE);
        console_write("|\r\n");
    }
    draw_border();
}

/** Restore the exact glyph and background of an occupied court cell. */
static void restore_cell(int x, int y)
{
    cursor_at(FIELD_ROW + y, FIELD_COLUMN + x);
    style(COURT, FIELD);
    char tile[] = {court_tile(x, y), '\0'};
    console_write(tile);
}

/** Show the point winner and next serve, or the final result. */
static void draw_status(void)
{
    cursor_at(STATUS_ROW, 1);
    style(INK, BASE);
    console_write("\033[2K");
    switch (match.phase) {
    case PONG_SERVE:
        if (match.last_point == PONG_PLAYER_POINT) {
            style(PLAYER, BASE);
            console_write("YOUR POINT  |  ");
        } else if (match.last_point == PONG_CPU_POINT) {
            style(CPU, BASE);
            console_write("CPU POINT  |  ");
        }
        style(PEACH, BASE);
        console_write(match.dx > 0 ? "Serve -> CPU" : "Serve <- YOU");
        break;
    case PONG_WON:
        style(PEACH, BASE);
        console_write("YOU WIN!");
        style(INK, BASE);
        console_write("  |  Final ");
        write_score();
        break;
    case PONG_LOST:
        style(CPU, BASE);
        console_write("CPU WINS!");
        style(INK, BASE);
        console_write("  |  Final ");
        write_score();
        break;
    case PONG_RALLY:
        break;
    }
}

/** Show only the controls available in the current match phase. */
static void draw_controls(void)
{
    cursor_at(CONTROLS_ROW, 1);
    style(PEACH, BASE);
    console_write("\033[2K");
    if (match.phase == PONG_LOST) {
        console_write("Q  Menu");
    } else if (match.phase == PONG_WON) {
        console_write(match.level < PONG_MAX_LEVEL ? "N  Next level   |   Q  Menu"
                                                   : "Final level    |   Q  Menu");
    } else {
        console_write("W/S  Move      |   Q  Menu");
    }
}

/** Return whether the controls show a finished match. */
static bool has_result(PongPhase phase)
{
    return phase == PONG_WON || phase == PONG_LOST;
}

/** Update a paddle, erasing only the cells that its movement left empty. */
static void draw_paddle(int x, int top, int old_top, Color color)
{
    for (int row = old_top; !fresh_screen && row < old_top + PONG_PADDLE_SIZE; ++row) {
        if (row < top || row >= top + PONG_PADDLE_SIZE) {
            restore_cell(x, row);
        }
    }
    for (int row = top; row < top + PONG_PADDLE_SIZE; ++row) {
        if (fresh_screen || row < old_top || row >= old_top + PONG_PADDLE_SIZE) {
            cursor_at(FIELD_ROW + row, FIELD_COLUMN + x);
            style(INK, color);
            console_write(" ");
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
    case 'q': case 'Q': reset_style(); exit_requested = true; break;
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
    bool score_changed = drawn.player_score != match.player_score ||
                         drawn.cpu_score != match.cpu_score;
    if (fresh_screen || score_changed || drawn.level != match.level) {
        draw_score();
    }
    if (fresh_screen || drawn.level != match.level) {
        draw_settings();
    }
    draw_paddle(0, match.player_y, drawn.player_y, PLAYER);
    draw_paddle(PONG_WIDTH - 1, match.cpu_y, drawn.cpu_y, CPU);

    if (fresh_screen || drawn.ball_x != match.ball_x || drawn.ball_y != match.ball_y) {
        if (!fresh_screen) {
            restore_cell(drawn.ball_x, drawn.ball_y);
        }
        cursor_at(FIELD_ROW + match.ball_y, FIELD_COLUMN + match.ball_x);
        style(INK, FIELD);
        console_write("O");
    }
    if (fresh_screen || drawn.phase != match.phase || drawn.level != match.level ||
        (match.phase == PONG_SERVE && score_changed)) {
        draw_status();
    }
    if (fresh_screen || has_result(drawn.phase) != has_result(match.phase)) {
        draw_controls();
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
