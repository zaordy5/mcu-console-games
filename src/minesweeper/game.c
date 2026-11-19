#include "minesweeper.h"
#include "board.h"

/** Campaign length, generation budget and incomplete arrow timeout in milliseconds. */
enum { LEVELS = 3, SEARCH_LIMIT = 128, BANDED_ATTEMPTS = 24, ESCAPE_TIMEOUT = 125 };

/** Progress through play, generation, and result screens. */
typedef enum { PLAYING, SEARCHING, CLEARED, LOST, RETURN_TO_MENU } SessionState;

/** Incomplete input from ANSI cursor and application cursor keys. */
typedef enum { KEY_BYTE, KEY_ESCAPE, KEY_ARROW } InputState;

/** Board and presentation state for the one active game instance. */
static struct {
    MinesBoard board;          /**< Current level's field. */
    GameDifficulty difficulty; /**< Mine density selected by the player. */
    unsigned level;            /**< Zero-based level index. */
    uint8_t x, y;              /**< Cursor location in the field. */
    uint8_t first;             /**< Starting cell for replay and generation. */
    MinesLayout layout;        /**< Placement family of the replayable board. */
    uint32_t seed;             /**< Seed of the current candidate or accepted field. */
    uint32_t elapsed_ms;       /**< Time input used to vary new fields. */
    uint32_t played_ms;        /**< Time since the first open, frozen at the result. */
    unsigned attempts_left;    /**< Remaining solver checks for this first click. */
    SessionState state;        /**< Active game or result screen. */
    InputState input;          /**< State of an incomplete escape sequence. */
    uint32_t escape_ms;        /**< Age of the current escape sequence. */
    bool logical;              /**< Require a solution with local deductions. */
    bool search_failed;        /**< Show the retry message after the search budget. */
    bool dirty;                /**< Refresh the entire screen on board changes. */
    bool hud_dirty;            /**< Refresh only the header on a timer change. */
} session;

/** Odd stride spreads nearby generator states across the 32-bit seed space. */
static const uint32_t seed_stride = 0x9e3779b9u;

/** Vary the layout even when two games receive identical key timings. */
static uint32_t entropy;

/** Prepare a new board while retaining the difficulty and solver preference. */
static void prepare_level(unsigned level)
{
    unsigned side = 8 + 2 * level;
    unsigned density = 18;

    if (session.difficulty == GAME_EASY) {
        density = 12;
    } else if (session.difficulty == GAME_HARD) {
        density = 25;
    }
    mines_board_reset(&session.board, side, side * side * density / 100);
    session.level = level;
    session.x = 0;
    session.y = 0;
    session.played_ms = 0;
    session.state = PLAYING;
    session.input = KEY_BYTE;
    session.search_failed = false;
    session.dirty = true;
    session.hud_dirty = false;
}

/** Start a new campaign with the difficulty from the shared menu. */
static void start_game(GameDifficulty difficulty)
{
    session.difficulty = difficulty;
    session.elapsed_ms = 0;
    session.logical = false;
    prepare_level(0);
}

/** Apply the result of revealing a cell or its neighbors. */
static void apply_result(MinesResult result)
{
    if (result == MINES_NO_CHANGE) {
        return;
    }
    if (result == MINES_WON) {
        session.state = CLEARED;
    } else if (result == MINES_LOST) {
        session.state = LOST;
    }
    session.dirty = true;
}

/** Generate a candidate and accept it only if the solver clears it. */
static void search_step(void)
{
    if (session.attempts_left == 1) {
        session.layout = MINES_COMPACT;
    } else if (session.attempts_left <= BANDED_ATTEMPTS) {
        session.layout = MINES_BANDED;
    }
    mines_board_generate(&session.board, session.first, session.seed, session.layout);
    if (mines_board_solvable(&session.board, session.first)) {
        session.state = PLAYING;
        apply_result(mines_open(&session.board, session.first));
        return;
    }

    session.board.generated = false;
    --session.attempts_left;
    if (session.attempts_left == 0) {
        session.state = PLAYING;
        session.search_failed = true;
        session.dirty = true;
    } else {
        session.seed += seed_stride;
    }
}

/** Maintain clocks and test at most one candidate per timer callback. */
static void tick(uint32_t elapsed_ms)
{
    session.elapsed_ms += elapsed_ms;
    if (session.input != KEY_BYTE) {
        session.escape_ms += elapsed_ms;
        if (session.escape_ms >= ESCAPE_TIMEOUT) {
            session.input = KEY_BYTE;
        }
    }
    if (session.state == SEARCHING) {
        search_step();
    } else if (session.state == PLAYING && session.board.generated) {
        uint32_t previous_seconds = session.played_ms / 1000;
        session.played_ms += elapsed_ms;
        if (session.played_ms / 1000 != previous_seconds) {
            session.hud_dirty = true;
        }
    }
}

/** Decode bare ANSI CSI and SS3 arrows, leaving ASCII commands unchanged. */
static char decode_key(char key)
{
    if (key == '\033') {
        session.input = KEY_ESCAPE;
        session.escape_ms = 0;
        return '\0';
    }
    if (session.input == KEY_ESCAPE) {
        session.input = (key == '[' || key == 'O') ? KEY_ARROW : KEY_BYTE;
        return '\0';
    }
    if (session.input == KEY_ARROW) {
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

/** Open a cell, generating or scheduling a layout on the first action. */
static void open_cell(uint8_t index)
{
    if (session.board.generated) {
        apply_result(mines_open(&session.board, index));
        return;
    }
    if (session.board.cells[index] & CELL_FLAG) {
        return;
    }

    session.first = index;
    session.layout = MINES_SCATTERED;
    entropy += seed_stride;
    session.seed = entropy ^ session.elapsed_ms;
    session.search_failed = false;

    if (session.logical) {
        session.attempts_left = SEARCH_LIMIT;
        session.state = SEARCHING;
        session.dirty = true;
    } else {
        mines_board_generate(&session.board, index, session.seed, session.layout);
        apply_result(mines_open(&session.board, index));
    }
}

/** Apply a movement or board action and request a redraw on changes. */
static void play_key(char key)
{
    uint8_t index = session.y * session.board.side + session.x;
    switch (key) {
    case 'w':
        if (session.y == 0) return;
        --session.y;
        break;
    case 's':
        if (session.y + 1 == session.board.side) return;
        ++session.y;
        break;
    case 'a':
        if (session.x == 0) return;
        --session.x;
        break;
    case 'd':
        if (session.x + 1 == session.board.side) return;
        ++session.x;
        break;
    case 'f':
        if (!mines_flag(&session.board, index)) return;
        break;
    case 'g':
        if (session.board.generated) return;
        session.logical = !session.logical;
        session.search_failed = false;
        break;
    case ' ':
    case '\r':
    case '\n':
        open_cell(index);
        return;
    default:
        return;
    }
    session.dirty = true;
}

/** Replay the current layout from its original first click with a fresh clock. */
static void replay(void)
{
    prepare_level(session.level);
    session.x = session.first % session.board.side;
    session.y = session.first / session.board.side;
    mines_board_generate(&session.board, session.first, session.seed, session.layout);
    apply_result(mines_open(&session.board, session.first));
}

/** Route input to the active board, generator, or result screen. */
static void key_input(char input)
{
    char key = decode_key(input);
    if (key == 'q') {
        session.state = RETURN_TO_MENU;
    } else if (session.state == PLAYING) {
        play_key(key);
    } else if (session.state == CLEARED || session.state == LOST) {
        if (key == 'n' && session.state == CLEARED && session.level + 1 < LEVELS) {
            prepare_level(session.level + 1);
        } else if (key == 'r') {
            prepare_level(0);
        } else if (key == 'b') {
            replay();
        }
    }
}

/** Render an unsigned decimal value without a hosted C library. */
static void write_number(uint32_t value)
{
    char digits[11];
    unsigned i = sizeof digits - 1;
    digits[i] = '\0';
    do {
        digits[--i] = '0' + value % 10;
        value /= 10;
    } while (value);
    console_write(digits + i);
}

/** Show level, difficulty, mine counter, timer and current generation mode. */
static void draw_header(void)
{
    console_write("\033[H\033[36mMinesweeper\033[0m  L");
    write_number(session.level + 1);
    console_write("/3  ");
    switch (session.difficulty) {
    case GAME_EASY: console_write("Easy"); break;
    case GAME_HARD: console_write("Hard"); break;
    default: console_write("Normal"); break;
    }

    console_write("  Mines ");
    write_number(session.board.mine_count);
    console_write("  Left ");
    int left = session.board.mine_count - session.board.flagged;
    if (left < 0) {
        console_write("-");
        left = -left;
    }
    write_number(left);
    console_write("  ");
    write_number(session.played_ms / 1000);
    console_write(session.logical ? "s  Logic" : "s  Classic");
    console_write("\033[K\r\n");
}

/** Render one cell with a symbol independent of its ANSI color. */
static void draw_cell(uint8_t index, bool selected)
{
    uint8_t cell = session.board.cells[index];
    char symbol = '.';
    const char *color = "\033[37m";

    if (session.state == LOST && index == session.board.hit) {
        symbol = '!';
        color = "\033[30;101m";
    } else if (cell & CELL_FLAG) {
        bool wrong = session.state == LOST && !(cell & CELL_MINE);
        symbol = wrong ? 'X' : 'F';
        color = wrong ? "\033[31m" : "\033[33m";
    } else if (session.state == LOST && (cell & CELL_MINE)) {
        symbol = '*';
        color = "\033[31m";
    } else if (cell & CELL_OPEN) {
        uint8_t nearby = mines_neighbors(&session.board, index);
        symbol = nearby ? '0' + nearby : ' ';
        color = "\033[36m";
    }

    char label[] = {selected ? '[' : ' ', symbol, selected ? ']' : ' ', '\0'};
    console_write(color);
    console_write(label);
    console_write("\033[0m");
}

/** List reproducible layout parameters below a finished game. */
static void draw_layout(void)
{
    console_write("\033[K\r\nLayout ");
    write_number(session.board.side);
    console_write("/");
    write_number(session.board.mine_count);
    console_write("/");
    write_number(session.first);
    console_write("/");
    write_number(session.seed);
    console_write("/");
    write_number(session.layout);
}

/** Display controls valid for the current game state. */
static void draw_controls(void)
{
    if (session.state == LOST) {
        console_write("\033[31mMine hit!\033[0m  B replay | R restart | Q menu");
        draw_layout();
    } else if (session.state == CLEARED) {
        if (session.level + 1 == LEVELS) {
            console_write("\033[32mCampaign complete!\033[0m");
        } else {
            console_write("\033[32mLevel cleared!\033[0m  N next |");
        }
        console_write("  B replay | R restart | Q menu");
        draw_layout();
    } else if (session.state == SEARCHING) {
        console_write("Checking layouts... Q menu");
    } else {
        console_write("Arrows/WASD move | Space/Enter open | F flag | Q menu");
        if (!session.board.generated) {
            console_write("\033[K\r\n");
            if (session.search_failed) {
                console_write("Try Space again or G to switch to Classic");
            } else {
                console_write("G toggle Logic before opening");
            }
        }
    }
}

/** Refresh the full board on actions and just the header on timer updates. */
static void draw(void)
{
    if (!session.dirty && !session.hud_dirty) {
        return;
    }
    draw_header();
    session.hud_dirty = false;
    if (!session.dirty) {
        return;
    }
    session.dirty = false;

    for (uint8_t y = 0; y < session.board.side; ++y) {
        for (uint8_t x = 0; x < session.board.side; ++x) {
            draw_cell(y * session.board.side + x, x == session.x && y == session.y);
        }
        console_write("\033[K\r\n");
    }
    draw_controls();
    console_write("\033[J");
}

/** Indicate that the application should restore the main menu. */
static bool finished(void)
{
    return session.state == RETURN_TO_MENU;
}

const Game minesweeper_game = {
    .name = "Minesweeper",
    .start = start_game,
    .key = key_input,
    .tick = tick,
    .draw = draw,
    .finished = finished,
};
