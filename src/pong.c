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
/** Accent for the player's score and point notice. */
#define PLAYER_COLOR "\033[1;36m"
/** Accent for the computer's score and point notice. */
#define CPU_COLOR "\033[1;35m"
/** Dim color for the static court and its markings. */
#define COURT_COLOR "\033[90m"
/** Return to the terminal's default colors. */
#define RESET_COLOR "\033[0m"

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

/** Write the score with the same accents as the two paddles. */
static void write_score(void)
{
    console_write(PLAYER_COLOR);
    write_number(match.player_score);
    console_write(RESET_COLOR " : " CPU_COLOR);
    write_number(match.cpu_score);
    console_write(RESET_COLOR);
}

/** Paint the centered score independently of the match settings. */
static void draw_score(void)
{
    cursor_at(1, SCORE_COLUMN);
    console_write("\033[2K" PLAYER_COLOR "YOU   " RESET_COLOR);
    write_score();
    console_write(CPU_COLOR "   CPU" RESET_COLOR);
}

/** Paint the current level, difficulty and target score. */
static void draw_settings(void)
{
    cursor_at(2, 3);
    console_write("\033[2K\033[1mPONG" RESET_COLOR "  |  L");
    write_number(match.level);
    console_write("  ");
    switch (match.difficulty) {
    case PONG_EASY: console_write("Easy"); break;
    case PONG_HARD: console_write("Hard"); break;
    default: console_write("Normal"); break;
    }
    console_write("  |  First to ");
    write_number(PONG_WIN_SCORE);
}

/** Skip cleared cells in the current terminal row without transmitting spaces. */
static void cursor_right(unsigned columns)
{
    char code[] = {
        '\033', '[', (char)('0' + columns / 10), (char)('0' + columns % 10), 'C', '\0'
    };
    console_write(code);
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
    console_write(RESET_COLOR "\033[2J\033[H\r\n\r\n" COURT_COLOR);
    draw_border();
    for (int y = 0; y < PONG_HEIGHT; ++y) {
        console_write("|");
        if (court_tile(PONG_WIDTH / 2, y) == '.') {
            cursor_right(PONG_WIDTH / 2);
            console_write(".");
            cursor_right(PONG_WIDTH - PONG_WIDTH / 2 - 1);
        } else {
            cursor_right(PONG_WIDTH);
        }
        console_write("|\r\n");
    }
    draw_border();
    console_write(RESET_COLOR);
}

/** Show the point winner and next serve, or the final result. */
static void draw_status(void)
{
    cursor_at(STATUS_ROW, 1);
    console_write("\033[2K");
    switch (match.phase) {
    case PONG_SERVE:
        if (match.last_point == PONG_PLAYER_POINT) {
            console_write(PLAYER_COLOR "YOUR POINT" RESET_COLOR "  |  ");
        } else if (match.last_point == PONG_CPU_POINT) {
            console_write(CPU_COLOR "CPU POINT" RESET_COLOR "  |  ");
        }
        console_write(match.dx > 0 ? "Serve -> CPU" : "Serve <- YOU");
        break;
    case PONG_WON:
        console_write("\033[1;32mYOU WIN!" RESET_COLOR "  |  Final ");
        write_score();
        break;
    case PONG_LOST:
        console_write("\033[1;31mCPU WINS!" RESET_COLOR "  |  Final ");
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
    bool score_changed = drawn.player_score != match.player_score ||
                         drawn.cpu_score != match.cpu_score;
    if (fresh_screen || score_changed || drawn.level != match.level) {
        draw_score();
    }
    if (fresh_screen || drawn.level != match.level) {
        draw_settings();
    }
    draw_paddle(FIELD_COLUMN, match.player_y, drawn.player_y, "\033[36;106m|" RESET_COLOR);
    draw_paddle(FIELD_COLUMN + PONG_WIDTH - 1, match.cpu_y, drawn.cpu_y, "\033[35;105m|" RESET_COLOR);

    if (fresh_screen || drawn.ball_x != match.ball_x || drawn.ball_y != match.ball_y) {
        if (!fresh_screen) {
            cursor_at(FIELD_ROW + drawn.ball_y, FIELD_COLUMN + drawn.ball_x);
            console_write(court_tile(drawn.ball_x, drawn.ball_y) == '.' ? COURT_COLOR "." RESET_COLOR : " ");
        }
        cursor_at(FIELD_ROW + match.ball_y, FIELD_COLUMN + match.ball_x);
        console_write("\033[1;97mO" RESET_COLOR);
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
