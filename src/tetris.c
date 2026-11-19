/** @file tetris.c
 *  Game callbacks, key bindings, and incremental ANSI terminal rendering.
 */
#include "tetris.h"
#include "tetris_core.h"

/** First terminal row of the board interior, using ANSI coordinates. */
#define FIELD_TOP 4
/** Terminal row for the result or exit message. */
#define RESULT_ROW (FIELD_TOP + TETRIS_HEIGHT + 2)
/** Width of the compact terminal layout and column where the cursor is parked. */
#define FRAME_WIDTH 40
/** Column of the drop timer on the title row. */
#define DROP_COLUMN 29
/** Minimum time between successful turns when the terminal repeats a held key. */
#define ROTATION_INTERVAL_MS 180u

/** Game session used by the synchronous firmware main loop. */
static Tetris session;
/** Colors last written to each terminal cell. */
static uint8_t shown[TETRIS_HEIGHT][TETRIS_WIDTH];
/** Line count last written to the status row. */
static uint32_t shown_lines;
/** The next draw must clear and redraw the frame. */
static bool new_frame;
/** A tick or key changed the visible game state. */
static bool dirty;
/** A session has started and the player has not returned to the menu. */
static bool session_active;
/** Time remaining before the next accepted turn, independent of gravity. */
static uint32_t rotation_wait_ms;

/** Input parser state for escape sequences emitted by the terminal. */
typedef enum {
    INPUT_KEY,
    INPUT_ESCAPE,
    INPUT_SEQUENCE
} InputState;

/** Input parser state for the active session. */
static InputState input_state;

/** ANSI background colors keyed by the portable cell value. */
static const char *const backgrounds[TETRIS_SHAPES + 1] = {
    "\033[0m", "\033[46m", "\033[104m", "\033[43m",
    "\033[47m", "\033[45m", "\033[42m", "\033[41m"
};

/** Display labels keyed by the portable starting difficulty. */
static const char *const difficulty_labels[] = { "Easy", "Normal", "Hard" };

/** Write an unsigned decimal number without a formatted I/O dependency. */
static void write_number(uint32_t number)
{
    char text[11];
    char *digit = &text[sizeof text - 1u];
    *digit = '\0';
    do {
        *--digit = (char)('0' + number % 10u);
        number /= 10u;
    } while (number != 0);
    console_write(digit);
}

/** Move the terminal cursor to a one-based position. */
static void move_cursor(unsigned row, unsigned column)
{
    console_write("\033[");
    write_number(row);
    console_write(";");
    write_number(column);
    console_write("H");
}

/** Write a horizontal border spanning the current board width. */
static void draw_border(void)
{
    console_write("\033[36m+");
    for (int x = 0; x < TETRIS_WIDTH; ++x) {
        console_write("--");
    }
    console_write("+\033[0m\r\n");
}

/** Draw the fixed frame and reset the displayed board cache. */
static void draw_frame(void)
{
    console_write("\033[0m\033[2J\033[H\033[1;36mTETRIS\033[0m | Difficulty: ");
    console_write(difficulty_labels[session.difficulty]);
    console_write("\r\n\r\n");
    draw_border();
    for (int y = 0; y < TETRIS_HEIGHT; ++y) {
        console_write("\033[36m|\033[0m");
        for (int x = 0; x < TETRIS_WIDTH; ++x) {
            console_write("  ");
            shown[y][x] = 0;
        }
        console_write("\033[36m|\033[0m\r\n");
    }
    draw_border();
    console_write("\033[36mMOVE\033[0m A/D or Left/Right  \033[36mTURN\033[0m W/Up\r\n");
    console_write("\033[36mDROP\033[0m S/Down  \033[36mMENU\033[0m Q");
}

/** Paint a run of adjacent cells sharing a background color. */
static void draw_run(int x, int y, int length, uint8_t color)
{
    char cells[2 * TETRIS_WIDTH + 1];
    int width = 2 * length;
    for (int i = 0; i < width; ++i) {
        cells[i] = ' ';
    }
    cells[width] = '\0';

    move_cursor((unsigned)(FIELD_TOP + y), (unsigned)(2 + 2 * x));
    console_write(backgrounds[color]);
    console_write(cells);
    if (color != 0) {
        console_write("\033[0m");
    }
}

/** Update only cells whose visible colors changed. */
static void draw_board(void)
{
    for (int y = 0; y < TETRIS_HEIGHT; ++y) {
        int x = 0;
        while (x < TETRIS_WIDTH) {
            uint8_t color = tetris_cell(&session, x, y);
            if (color == shown[y][x]) {
                ++x;
                continue;
            }
            int first = x;
            do {
                shown[y][x++] = color;
            } while (x < TETRIS_WIDTH && shown[y][x] != color &&
                     tetris_cell(&session, x, y) == color);
            draw_run(first, y, x - first, color);
        }
    }
}

/** Update the score and the millisecond interval displayed under the title. */
static void draw_status(void)
{
    move_cursor(1, DROP_COLUMN);
    console_write("\033[KDrop: ");
    write_number(tetris_interval(&session));
    console_write(" ms");
    move_cursor(2, 1);
    console_write("\033[2KScore ");
    write_number(session.score);
    console_write(" L");
    write_number(tetris_level(&session));
    console_write(" Ln");
    write_number(session.lines);
    shown_lines = session.lines;
}

/** Translate the shared difficulty to the portable rules and start a new game. */
static void start_game(GameDifficulty difficulty)
{
    TetrisDifficulty setting = TETRIS_NORMAL;
    if (difficulty == GAME_EASY) {
        setting = TETRIS_EASY;
    } else if (difficulty == GAME_HARD) {
        setting = TETRIS_HARD;
    }
    tetris_reset(&session, setting, 0);
    new_frame = true;
    dirty = true;
    session_active = true;
    input_state = INPUT_KEY;
    rotation_wait_ms = 0;
}

/** Decode ANSI arrows and discard unrelated terminal sequences. */
static char filter_key(char key)
{
    if (key == '\033') {
        input_state = INPUT_ESCAPE;
        return '\0';
    }
    if (input_state == INPUT_SEQUENCE) {
        if (key >= ' ' && key <= '?') {
            return '\0';
        }
        input_state = INPUT_KEY;
        switch (key) {
        case 'A': return 'w';
        case 'B': return 's';
        case 'C': return 'd';
        case 'D': return 'a';
        default: return '\0';
        }
    }
    if (input_state == INPUT_ESCAPE) {
        if (key == '[' || key == 'O') {
            input_state = INPUT_SEQUENCE;
            return '\0';
        }
        input_state = INPUT_KEY;
    }
    return key;
}

/** Process the current key, leaving the result screen on Q. */
static void handle_key(char key)
{
    if (!session_active) {
        return;
    }
    key = filter_key(key);
    if (key == 'q' || key == 'Q') {
        session_active = false;
        return;
    }
    if (session.lost) {
        return;
    }
    switch (key) {
    case 'a':
    case 'A':
        dirty |= tetris_move(&session, -1);
        break;
    case 'd':
    case 'D':
        dirty |= tetris_move(&session, 1);
        break;
    case 'w':
    case 'W':
        if (rotation_wait_ms == 0 && tetris_rotate(&session)) {
            dirty = true;
            rotation_wait_ms = ROTATION_INTERVAL_MS;
        }
        break;
    case 's':
    case 'S':
        dirty |= tetris_soft_drop(&session);
        break;
    default:
        break;
    }
}

/** Advance the portable rules according to elapsed firmware time. */
static void tick_game(uint32_t elapsed_ms)
{
    if (session_active) {
        rotation_wait_ms = elapsed_ms >= rotation_wait_ms ? 0 : rotation_wait_ms - elapsed_ms;
        dirty |= tetris_tick(&session, elapsed_ms);
    }
}

/** Flush pending frame changes and show the result until the player returns. */
static void draw_game(void)
{
    if (!session_active || !dirty) {
        return;
    }
    if (new_frame) {
        draw_frame();
    }
    if (new_frame || shown_lines != session.lines) {
        draw_status();
    }
    draw_board();
    if (session.lost) {
        move_cursor(RESULT_ROW, 1);
        console_write("\033[2K\033[31mGame over.\033[0m  Q - menu");
    }
    move_cursor(RESULT_ROW, FRAME_WIDTH);

    new_frame = false;
    dirty = false;
}

/** Report that the module is idle or the player returned to the menu. */
static bool game_finished(void)
{
    return !session_active;
}

/** Menu entry for the firmware's falling-blocks game. */
const Game tetris_game = {
    .name = "Tetris",
    .start = start_game,
    .key = handle_key,
    .tick = tick_game,
    .draw = draw_game,
    .finished = game_finished
};
