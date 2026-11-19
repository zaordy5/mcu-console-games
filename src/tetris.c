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
/** First row of the next-piece preview interior. */
#define NEXT_TOP 6
/** First text column occupied by preview cells. */
#define NEXT_LEFT 30
/** Minimum time between successful turns when the terminal repeats a held key. */
#define ROTATION_INTERVAL_MS 180u
/** Time to discard an incomplete terminal key sequence. */
#define INPUT_SEQUENCE_TIMEOUT_MS 150u
/** Cached cell marker for a projected landing outline. */
#define GHOST_CELL (TETRIS_SHAPES + 1u)
/** Screen row of the four-line notification, below the preview. */
#define QUAD_ROW 12
/** Screen column of the notification, to the right of the field. */
#define QUAD_COLUMN 28
/** Duration of the four-line notification. */
#define QUAD_NOTICE_MS 1200u

/** Game session used by the synchronous firmware main loop. */
static Tetris session;
/** Colors last written to each terminal cell. */
static uint8_t shown[TETRIS_HEIGHT][TETRIS_WIDTH];
/** Line count last written to the status row. */
static uint32_t shown_lines;
/** Color of the next piece last drawn in the preview. */
static uint8_t shown_next;
/** Four-line clears already handled by the renderer. */
static uint32_t handled_quads;
/** Remaining time of the four-line notification. */
static uint32_t quad_notice_ms;
/** Whether the notification is on screen. */
static bool quad_notice_drawn;
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
/** Time before an incomplete escape sequence is discarded. */
static uint32_t input_wait_ms;

/** Retrowave color roles matching the Minesweeper console. */
typedef enum {
    INK, MUTED, PINK, PEACH, LAVENDER, MINT, MELON, ICE, CREAM,
    BASE, FIELD, SUNSET_VIOLET, SUNSET_PINK, SUNSET_PEACH, NO_COLOR
} Color;

/** Decimal RGB channels for ANSI SGR. */
static const char *const rgb[] = {
    [INK] = "255;238;250", [MUTED] = "205;170;209", [PINK] = "255;133;199",
    [PEACH] = "255;203;151", [LAVENDER] = "217;190;255", [MINT] = "162;233;218",
    [MELON] = "255;168;163", [ICE] = "194;217;255", [CREAM] = "255;229;183",
    [BASE] = "24;15;39", [FIELD] = "36;22;56", [SUNSET_VIOLET] = "112;63;162",
    [SUNSET_PINK] = "220;84;155", [SUNSET_PEACH] = "255;172;123"
};

/** Figure backgrounds in core order I, J, L, O, T, S, Z. */
static const Color piece_colors[TETRIS_SHAPES] = {
    ICE, LAVENDER, PEACH, CREAM, PINK, MINT, MELON
};

/** Last ANSI foreground color written to the terminal. */
static Color foreground = NO_COLOR;
/** Last ANSI background color written to the terminal. */
static Color background = NO_COLOR;

/** Display labels keyed by the portable starting difficulty. */
static const char *const difficulty_labels[] = { "Easy", "Normal", "Hard" };

/** Emit only channels that changed since the previous write. */
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

/** Return the terminal to its normal colors and invalidate the style cache. */
static void reset_style(void)
{
    console_write("\033[0m");
    foreground = NO_COLOR;
    background = NO_COLOR;
}

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

/** Write a horizontal border in the current style. */
static void draw_border(int width)
{
    console_write("+");
    for (int x = 0; x < width; ++x) {
        console_write("--");
    }
    console_write("+\r\n");
}

/** Write a run of spaces without changing the selected style. */
static void blanks(int count)
{
    static const char spaces[2 * TETRIS_WIDTH + 1] = "                        ";
    console_write(spaces + 2 * TETRIS_WIDTH - count);
}

/** Paint a run of board or preview cells with the same display value. */
static void paint_cells(int length, uint8_t color)
{
    if (color == 0) {
        style(foreground, FIELD);
        blanks(2 * length);
        return;
    }

    char cells[2 * TETRIS_WIDTH + 1];
    bool ghost = color == GHOST_CELL;
    style(ghost ? MUTED : BASE, ghost ? FIELD : piece_colors[color - 1u]);
    for (int i = 0; i < length; ++i) {
        cells[2 * i] = '[';
        cells[2 * i + 1] = ']';
    }
    cells[2 * length] = '\0';

    console_write(cells);
}

/** Draw the three sunset colors across the upper border of the field. */
static void draw_sunset(void)
{
    const int stripe_width = 2 * TETRIS_WIDTH / 3;
    style(PINK, BASE);
    console_write("+");
    style(PINK, SUNSET_VIOLET);
    blanks(stripe_width);
    style(PINK, SUNSET_PINK);
    blanks(stripe_width);
    style(PINK, SUNSET_PEACH);
    blanks(2 * TETRIS_WIDTH - 2 * stripe_width);
    style(PINK, BASE);
    console_write("+\r\n");
}

/** Draw the fixed frame and reset the displayed board cache. */
static void draw_frame(void)
{
    reset_style();
    style(PINK, BASE);
    console_write("\033[2J\033[HTETRIS");
    style(INK, BASE);
    console_write(" | Difficulty: ");
    console_write(difficulty_labels[session.difficulty]);
    console_write("\r\n\r\n");
    draw_sunset();
    for (int y = 0; y < TETRIS_HEIGHT; ++y) {
        console_write("|");
        paint_cells(TETRIS_WIDTH, 0);
        for (int x = 0; x < TETRIS_WIDTH; ++x) {
            shown[y][x] = 0;
        }
        style(PINK, BASE);
        console_write("|\r\n");
    }
    draw_border(TETRIS_WIDTH);
    style(PEACH, BASE);
    console_write("MOVE");
    style(INK, BASE);
    console_write(" A/D or Left/Right  ");
    style(PEACH, BASE);
    console_write("TURN");
    style(INK, BASE);
    console_write(" W/Up\r\n");
    style(PEACH, BASE);
    console_write("DROP");
    style(INK, BASE);
    console_write(" S/Down  ");
    style(PEACH, BASE);
    console_write("HARD");
    style(INK, BASE);
    console_write(" Space  ");
    style(PEACH, BASE);
    console_write("MENU");
    style(INK, BASE);
    console_write(" Q");
    move_cursor(NEXT_TOP - 2, NEXT_LEFT - 1);
    style(PINK, BASE);
    console_write("Next block:");
    move_cursor(NEXT_TOP - 1, NEXT_LEFT - 1);
    draw_border(TETRIS_PIECE_SIZE);
    for (unsigned y = 0; y < TETRIS_PIECE_SIZE; ++y) {
        move_cursor(NEXT_TOP + y, NEXT_LEFT - 1);
        console_write("|");
        paint_cells(TETRIS_PIECE_SIZE, 0);
        style(PINK, BASE);
        console_write("|");
    }
    move_cursor(NEXT_TOP + TETRIS_PIECE_SIZE, NEXT_LEFT - 1);
    draw_border(TETRIS_PIECE_SIZE);
}

/** Overlay the landing hint on otherwise empty cells. */
static uint8_t visible_cell(int x, int y, int landing)
{
    uint8_t color = tetris_cell(&session, x, y);
    if (color == 0 && y >= landing && y < landing + TETRIS_PIECE_SIZE &&
        tetris_piece_at(&session, x, y, landing)) {
        return GHOST_CELL;
    }
    return color;
}

/** Paint each contiguous span of changed cells with a single cursor move. */
static void draw_board(void)
{
    int landing = tetris_landing_y(&session);
    for (int y = 0; y < TETRIS_HEIGHT; ++y) {
        uint8_t row[TETRIS_WIDTH];
        for (int col = 0; col < TETRIS_WIDTH; ++col) {
            row[col] = visible_cell(col, y, landing);
        }
        int x = 0;
        while (x < TETRIS_WIDTH) {
            if (row[x] == shown[y][x]) {
                ++x;
                continue;
            }
            move_cursor((unsigned)(FIELD_TOP + y), (unsigned)(2 + 2 * x));
            do {
                uint8_t color = row[x];
                int first = x;
                do {
                    shown[y][x++] = color;
                } while (x < TETRIS_WIDTH && shown[y][x] != color && row[x] == color);
                paint_cells(x - first, color);
            } while (x < TETRIS_WIDTH && row[x] != shown[y][x]);
        }
    }
}

/** Announce a four-line clear, then erase the message when its timer expires. */
static void draw_quad_notice(void)
{
    if (handled_quads != session.quad_clears) {
        handled_quads = session.quad_clears;
        quad_notice_ms = QUAD_NOTICE_MS;
    }
    bool visible = quad_notice_ms != 0;
    if (quad_notice_drawn == visible) {
        return;
    }
    move_cursor(QUAD_ROW, QUAD_COLUMN);
    style(PEACH, BASE);
    if (visible) {
        console_write("4 LINES +");
        write_number(TETRIS_QUAD_SCORE);
    } else {
        console_write("\033[K");
    }
    quad_notice_drawn = visible;
}

/** Draw or clear the next tetromino, keeping its outline separate from the board. */
static void draw_next(void)
{
    for (int y = 0; y < TETRIS_PIECE_SIZE; ++y) {
        move_cursor((unsigned)(NEXT_TOP + y), NEXT_LEFT);
        for (int x = 0; x < TETRIS_PIECE_SIZE; ++x) {
            paint_cells(1, tetris_next_cell(&session, x, y));
        }
    }
    shown_next = tetris_next_color(&session);
}

/** Update the score and the millisecond interval displayed under the title. */
static void draw_status(void)
{
    move_cursor(1, DROP_COLUMN);
    style(PEACH, BASE);
    console_write("\033[KDrop: ");
    write_number(tetris_interval(&session));
    console_write(" ms");
    move_cursor(2, 1);
    style(INK, BASE);
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
    input_wait_ms = 0;
    rotation_wait_ms = 0;
    handled_quads = 0;
    quad_notice_ms = 0;
    quad_notice_drawn = false;
}

/** Decode ANSI arrows and discard unrelated terminal sequences. */
static char filter_key(char key)
{
    if (key == '\033') {
        input_state = INPUT_ESCAPE;
        input_wait_ms = INPUT_SEQUENCE_TIMEOUT_MS;
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
        reset_style();
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
    case ' ':
        dirty |= tetris_hard_drop(&session);
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
        if (input_state != INPUT_KEY) {
            if (elapsed_ms >= input_wait_ms) {
                input_state = INPUT_KEY;
            } else {
                input_wait_ms -= elapsed_ms;
            }
        }
        if (quad_notice_ms != 0) {
            if (elapsed_ms >= quad_notice_ms) {
                quad_notice_ms = 0;
                dirty = true;
            } else {
                quad_notice_ms -= elapsed_ms;
            }
        }
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
    if (new_frame || shown_next != tetris_next_color(&session)) {
        draw_next();
    }
    draw_board();
    draw_quad_notice();
    if (session.lost) {
        move_cursor(RESULT_ROW, 1);
        style(PINK, BASE);
        console_write("\033[2KGame over.");
        style(INK, BASE);
        console_write("  Q - menu");
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
