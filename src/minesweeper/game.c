#include "minesweeper.h"
#include "view.h"

/** Bounds on candidate checks, escape sequences, and redraws during pasted input. */
enum {
    SEARCH_LIMIT = 128, BANDED_ATTEMPTS = 24,
    ESCAPE_TIMEOUT_MS = 125, EDIT_QUIET_MS = 40
};

/** Incomplete input from ANSI cursor and application cursor keys. */
typedef enum { KEY_BYTE, KEY_ESCAPE, KEY_CSI, KEY_SS3 } InputState;

/** Two fixed slots let the player switch between a new game and a suspended one. */
static MinesRun runs[2];

/** Controller state, separate from the board and timer saved in a run. */
static struct {
    MinesRun *run;                  /**< Current run. */
    MinesRun *saved;                /**< Suspended run, if any. */
    MinesScreen screen;             /**< Current board, form, or pause screen. */
    InputState input;               /**< State of an incomplete ANSI sequence. */
    uint32_t escape_ms;             /**< Age of the current ANSI sequence. */
    uint32_t elapsed_ms;            /**< Input timing used when making new fields. */
    unsigned attempts_left;        /**< Remaining solver checks for this first move. */
    char field[MINES_CODE_CAPACITY]; /**< Editable layout code or mine count. */
    uint8_t field_len;              /**< Number of stored characters. */
    unsigned excess_chars;          /**< Extra characters that must be erased. */
    uint8_t custom_side;            /**< Size selected in the custom field form. */
    uint32_t edit_wait_ms;          /**< Delay before painting input so UART pastes can finish. */
    bool invalid_field;             /**< Show the form's validation error. */
    bool last_cr;                   /**< Suppress LF immediately following a CR. */
    bool search_failed;             /**< Show a retry message when candidate checks fail. */
    bool dirty;                     /**< Update the board or current screen. */
    bool hud_dirty;                 /**< Update only the board's header. */
} session;

/** Odd stride spreads nearby generator states across the 32-bit seed space. */
static const uint32_t seed_stride = 0x9e3779b9u;

/** Vary the layout when two games receive identical key timings. */
static uint32_t entropy;

/** Switch screens and request a redraw. */
static void show(MinesScreen screen)
{
    session.screen = screen;
    session.edit_wait_ms = 0;
    session.dirty = true;
    session.hud_dirty = false;
}

/** Start a new field of this run's chosen size and mine count. */
static void prepare_board(uint8_t side, uint8_t count)
{
    MinesRun *run = session.run;
    mines_board_reset(&run->board, side, count);
    run->cursor = 0;
    run->played_ms = 0;
    session.search_failed = false;
    mines_view_invalidate();
    show(MINES_PLAY);
}

/** Prepare the numbered campaign level with this run's difficulty. */
static void prepare_level(unsigned level)
{
    MinesRun *run = session.run;
    unsigned side = MINES_MIN_SIDE + 2 * level;
    unsigned density = 18;
    if (run->difficulty == GAME_EASY) {
        density = 12;
    } else if (run->difficulty == GAME_HARD) {
        density = 25;
    }
    run->mode = MINES_CAMPAIGN;
    run->level = level;
    prepare_board(side, side * side * density / 100);
}

/** Enter Minesweeper and reserve the other slot if a run is suspended. */
static void start_game(GameDifficulty difficulty)
{
    session.run = session.saved == &runs[0] ? &runs[1] : &runs[0];
    session.run->difficulty = difficulty;
    session.run->logical = false;
    session.elapsed_ms = 0;
    session.input = KEY_BYTE;
    session.last_cr = false;
    prepare_level(0);
}

/** Apply the result of revealing a cell or its neighbors. */
static void apply_result(MinesResult result)
{
    if (result == MINES_WON) {
        show(MINES_WIN);
    } else if (result == MINES_LOST) {
        show(MINES_LOSS);
    } else if (result == MINES_OPENED) {
        session.dirty = true;
    }
}

/** Reveal the first cell on an existing layout and start visual feedback. */
static void reveal_first(MinesRun *run)
{
    run->cursor = run->first;
    apply_result(mines_open(&run->board, run->first));
    if (session.screen == MINES_PLAY) {
        mines_view_first_reveal();
    }
}

/** Generate a candidate and accept it only if the solver clears it. */
static void search_step(void)
{
    MinesRun *run = session.run;
    if (session.attempts_left == 1) {
        run->layout = MINES_COMPACT;
    } else if (session.attempts_left <= BANDED_ATTEMPTS) {
        run->layout = MINES_BANDED;
    }
    mines_board_generate(&run->board, run->first, run->seed, run->layout);
    if (mines_board_solvable(&run->board, run->first)) {
        show(MINES_PLAY);
        reveal_first(run);
        return;
    }

    run->board.generated = false;
    --session.attempts_left;
    if (session.attempts_left == 0) {
        session.search_failed = true;
        show(MINES_PLAY);
    } else {
        run->seed += seed_stride;
    }
}

/** Maintain clocks and test at most one candidate per timer callback. */
static void tick(uint32_t elapsed_ms)
{
    session.elapsed_ms += elapsed_ms;
    if (session.input != KEY_BYTE) {
        session.escape_ms += elapsed_ms;
        if (session.escape_ms >= ESCAPE_TIMEOUT_MS) {
            session.input = KEY_BYTE;
        }
    }

    if (session.edit_wait_ms != 0) {
        if (elapsed_ms < session.edit_wait_ms) {
            session.edit_wait_ms -= elapsed_ms;
        } else {
            session.edit_wait_ms = 0;
            session.dirty = true;
        }
    }

    MinesRun *run = session.run;
    if (session.screen == MINES_SEARCH) {
        search_step();
    } else if (session.screen == MINES_PLAY && run->board.generated) {
        if (mines_view_tick(elapsed_ms)) {
            session.dirty = true;
        }
        uint32_t previous_ms = run->played_ms;
        uint32_t next = previous_ms + elapsed_ms;
        run->played_ms = next >= previous_ms ? next : UINT32_MAX;
        if (run->played_ms / 1000 != previous_ms / 1000) {
            session.hud_dirty = true;
        }
    }
}

/** Decode ANSI arrows and skip unrelated CSI sequences, including paste wrappers. */
static char decode_key(char key)
{
    if (key == '\033') {
        session.input = KEY_ESCAPE;
        session.escape_ms = 0;
        return '\0';
    }
    if (session.input == KEY_ESCAPE) {
        if (key == '[') {
            session.input = KEY_CSI;
        } else if (key == 'O') {
            session.input = KEY_SS3;
        } else {
            session.input = KEY_BYTE;
        }
        return '\0';
    }
    if (session.input == KEY_CSI || session.input == KEY_SS3) {
        if (session.input == KEY_CSI && key >= ' ' && key <= '?') {
            return '\0';
        }
        session.input = KEY_BYTE;
        switch (key) {
        case 'A': return 'w';
        case 'B': return 's';
        case 'C': return 'd';
        case 'D': return 'a';
        default:  return '\0';
        }
    }
    if (key >= 'A' && key <= 'Z') {
        return (char)(key + ('a' - 'A'));
    }
    return key;
}

/** Open the recorded starting cell of an exact generated or replayed field. */
static void open_first(MinesRun *run)
{
    mines_board_generate(&run->board, run->first, run->seed, run->layout);
    reveal_first(run);
}

/** Open the cursor, starting generation if it is the first action. */
static void open_cell(MinesRun *run)
{
    if (run->board.generated) {
        apply_result(mines_open(&run->board, run->cursor));
        return;
    }
    if (run->board.cells[run->cursor] & CELL_FLAG) {
        return;
    }

    run->first = run->cursor;
    run->layout = MINES_SCATTERED;
    entropy += seed_stride;
    run->seed = entropy ^ session.elapsed_ms;
    session.search_failed = false;
    if (run->logical) {
        session.attempts_left = SEARCH_LIMIT;
        show(MINES_SEARCH);
    } else {
        open_first(run);
    }
}

/** Move the field cursor without wrapping across its edges. */
static void move_cursor(MinesRun *run, int dx, int dy)
{
    int side = run->board.side;
    int x = run->cursor % side + dx;
    int y = run->cursor / side + dy;
    if (x >= 0 && x < side && y >= 0 && y < side) {
        run->cursor = (uint8_t)(y * side + x);
        session.dirty = true;
    }
}

/** Open an editor with an empty buffer. */
static void start_form(MinesScreen screen)
{
    session.field_len = 0;
    session.field[0] = '\0';
    session.excess_chars = 0;
    session.invalid_field = false;
    show(screen);
}

/** Start a single imported board from the code's original first move. */
static void load_layout(const MinesCode *code)
{
    MinesRun *run = session.run;
    run->mode = MINES_SHARED;
    run->logical = false;
    prepare_board(code->side, code->mine_count);
    run->first = code->first;
    run->seed = code->seed;
    run->layout = code->type;
    open_first(run);
}

/** Confirm a custom field or load a layout after validating the whole input. */
static void submit_form(void)
{
    if (session.excess_chars == 0) {
        if (session.screen == MINES_COUNT_FORM) {
            uint8_t count;
            if (mines_parse_count(session.field, session.custom_side, &count)) {
                session.run->mode = MINES_CUSTOM;
                session.run->logical = false;
                prepare_board(session.custom_side, count);
                return;
            }
        } else {
            MinesCode code;
            if (mines_parse_code(session.field, &code)) {
                load_layout(&code);
                return;
            }
        }
    }
    session.invalid_field = true;
    session.edit_wait_ms = 0;
    session.dirty = true;
}

/** Route commands while entering a board size, mine count, or layout code. */
static void form_key(char key)
{
    if (key == 'q') {
        show(MINES_PLAY);
        return;
    }
    if (session.screen == MINES_SIZE_FORM) {
        if (key >= '1' && key <= '3') {
            session.custom_side = MINES_MIN_SIDE + 2 * (key - '1');
            start_form(MINES_COUNT_FORM);
        }
        return;
    }
    if (key == '\r' || key == '\n') {
        submit_form();
        return;
    }
    if (key == '\b' || key == '\x7f') {
        if (session.excess_chars != 0) {
            --session.excess_chars;
        } else if (session.field_len != 0) {
            session.field[--session.field_len] = '\0';
        }
    } else if ((key >= '0' && key <= '9') || (key == '/' && session.screen == MINES_LOAD_FORM)) {
        if (session.field_len == sizeof session.field - 1) {
            ++session.excess_chars;
        } else {
            session.field[session.field_len++] = key;
            session.field[session.field_len] = '\0';
        }
    } else {
        return;
    }

    session.invalid_field = false;
    session.edit_wait_ms = EDIT_QUIET_MS;
}

/** Process actions available in a board that has yet to be opened. */
static void setup_key(MinesRun *run, char key)
{
    if (run->board.generated) {
        return;
    }

    if (key == 'g' && mines_board_logic_allowed(&run->board)) {
        run->logical = !run->logical;
        session.search_failed = false;
        session.dirty = true;
    } else if (key == 'c') {
        start_form(MINES_SIZE_FORM);
    } else if (key == 'l') {
        start_form(MINES_LOAD_FORM);
    } else if (key == 'u' && session.saved) {
        session.run = session.saved;
        session.saved = 0;
        mines_view_invalidate();
        show(MINES_PLAY);
    }
}

/** Apply a movement, mark, reveal, or setup action. */
static void play_key(char key)
{
    MinesRun *run = session.run;
    switch (key) {
    case 'w': move_cursor(run, 0, -1); break;
    case 's': move_cursor(run, 0, 1); break;
    case 'a': move_cursor(run, -1, 0); break;
    case 'd': move_cursor(run, 1, 0); break;
    case 'f':
        if (mines_flag(&run->board, run->cursor)) session.dirty = true;
        break;
    case '?':
        if (mines_question(&run->board, run->cursor)) session.dirty = true;
        break;
    case ' ':
    case '\r':
    case '\n': open_cell(run); break;
    case 'p': show(MINES_PAUSE); break;
    default: setup_key(run, key); break;
    }
}

/** Replay a result, restart, or advance the campaign. */
static void result_key(char key)
{
    MinesRun *run = session.run;
    if (key == 'n' && session.screen == MINES_WIN && run->mode == MINES_CAMPAIGN &&
        run->level + 1 < MINES_LEVELS) {
        prepare_level(run->level + 1);
    } else if (key == 'b') {
        prepare_board(run->board.side, run->board.mine_count);
        open_first(run);
    } else if (key == 'r') {
        if (run->mode == MINES_CUSTOM) {
            prepare_board(run->board.side, run->board.mine_count);
        } else {
            prepare_level(0);
        }
    }
}

/** Return to the main menu, keeping at most one unfinished board. */
static void leave(void)
{
    if ((session.screen == MINES_PLAY || session.screen == MINES_PAUSE) &&
        session.run->board.generated) {
        session.saved = session.run;
    }
    show(MINES_EXIT);
}

/** Dispatch raw UART input, suppressing CRLF double actions. */
static void key_input(char input)
{
    bool repeated_enter = session.last_cr && input == '\n';
    session.last_cr = input == '\r';
    if (repeated_enter) {
        return;
    }

    char key = decode_key(input);
    if (key == '\0') {
        return;
    }

    if (session.screen == MINES_SIZE_FORM || session.screen == MINES_COUNT_FORM ||
        session.screen == MINES_LOAD_FORM) {
        form_key(key);
    } else if (key == 'q') {
        leave();
    } else if (session.screen == MINES_PLAY) {
        play_key(key);
    } else if (session.screen == MINES_PAUSE && key == 'p') {
        show(MINES_PLAY);
    } else if (session.screen == MINES_WIN || session.screen == MINES_LOSS) {
        result_key(key);
    }
}

/** Refresh just the clock line for a timer event or draw the active screen. */
static void draw(void)
{
    if (!session.dirty) {
        if (session.hud_dirty) {
            mines_view_hud(session.run);
            session.hud_dirty = false;
        }
        return;
    }
    session.dirty = false;
    session.hud_dirty = false;

    switch (session.screen) {
    case MINES_SIZE_FORM:
    case MINES_COUNT_FORM:
    case MINES_LOAD_FORM:
        mines_view_form(session.screen, session.custom_side, session.field, session.invalid_field);
        break;
    case MINES_PAUSE:
        mines_view_pause(session.run);
        break;
    case MINES_EXIT:
        break;
    default:
        mines_view_board(session.run, session.screen, session.saved != 0, session.search_failed);
        break;
    }
}

/** Indicate that the application should restore the main menu. */
static bool finished(void)
{
    return session.screen == MINES_EXIT;
}

const Game minesweeper_game = {
    .name = "Minesweeper",
    .start = start_game,
    .key = key_input,
    .tick = tick,
    .draw = draw,
    .finished = finished,
};
