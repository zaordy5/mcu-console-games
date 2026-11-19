#include "minesweeper.h"
#include "board.h"

/** Number of levels in a campaign. */
enum { LEVELS = 3 };

/** Progress through the active game and its result screens. */
typedef enum { PLAYING, CLEARED, LOST, RETURN_TO_MENU } SessionState;

/** Board and presentation state for the one active game instance. */
static struct {
    MinesBoard board;          /**< Current level's field. */
    GameDifficulty difficulty; /**< Mine density selected by the player. */
    unsigned level;            /**< Zero-based level index. */
    uint8_t x, y;              /**< Cursor location in the field. */
    uint32_t elapsed;          /**< Time used to vary the first-click layout. */
    SessionState state;        /**< Active game or result screen. */
    bool dirty;                /**< A changed frame needs to be sent to USART2. */
} session;

/** Prepare a new board while retaining the selected difficulty. */
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
    session.state = PLAYING;
    session.dirty = true;
}

/** Start a new campaign with the difficulty from the shared menu. */
static void start_game(GameDifficulty difficulty)
{
    session.difficulty = difficulty;
    session.elapsed = 0;
    prepare_level(0);
}

/** Record the delay between inputs to vary layouts across games. */
static void tick(uint32_t elapsed_ms)
{
    session.elapsed += elapsed_ms;
}

/** Return an uppercase ASCII command as lowercase. */
static char normalize(char key)
{
    if (key >= 'A' && key <= 'Z') {
        return (char)(key + ('a' - 'A'));
    }
    return key;
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
    case ' ':
    case '\r':
    case '\n': {
        MinesResult result = mines_open(&session.board, index,
                                        session.elapsed ^ ((session.level + 1) * 0x9e3779b9u));
        if (result == MINES_NO_CHANGE) return;
        if (result == MINES_WON) session.state = CLEARED;
        if (result == MINES_LOST) session.state = LOST;
        break;
    }
    default:
        return;
    }
    session.dirty = true;
}

/** Route input to the active board or to its result screen. */
static void key_input(char key)
{
    key = normalize(key);
    if (key == 'q') {
        session.state = RETURN_TO_MENU;
    } else if (session.state == PLAYING) {
        play_key(key);
    } else if (session.state == CLEARED && key == 'n' && session.level + 1 < LEVELS) {
        prepare_level(session.level + 1);
    } else if ((session.state == CLEARED || session.state == LOST) && key == 'r') {
        prepare_level(0);
    }
}

/** Render an unsigned decimal value without a hosted C library. */
static void write_number(unsigned value)
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

/** Render one cell with visible text and a color cue. */
static void draw_cell(uint8_t index, bool selected)
{
    uint8_t cell = session.board.cells[index];
    char symbol = '.';
    const char *color = "\033[37m";

    if ((cell & CELL_MINE) && session.state == LOST) {
        symbol = '*';
        color = "\033[31m";
    } else if (cell & CELL_FLAG) {
        symbol = 'F';
        color = "\033[33m";
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

/** Send a changed frame, including controls and result messages. */
static void draw(void)
{
    if (!session.dirty) {
        return;
    }
    session.dirty = false;

    console_write("\033[H\033[36mMinesweeper\033[0m  Level ");
    write_number(session.level + 1);
    console_write("/3  ");
    switch (session.difficulty) {
    case GAME_EASY: console_write("Easy"); break;
    case GAME_HARD: console_write("Hard"); break;
    default: console_write("Normal"); break;
    }
    console_write("  Mines ");
    write_number(session.board.mine_count);
    console_write("\033[K\r\n");
    for (uint8_t y = 0; y < session.board.side; ++y) {
        for (uint8_t x = 0; x < session.board.side; ++x) {
            draw_cell(y * session.board.side + x, x == session.x && y == session.y);
        }
        console_write("\033[K\r\n");
    }
    if (session.state == LOST) {
        console_write("\033[31mMine hit!\033[0m  R restart | Q menu");
    } else if (session.state == CLEARED) {
        if (session.level + 1 == LEVELS) {
            console_write("\033[32mCampaign complete!\033[0m  R restart | Q menu");
        } else {
            console_write("\033[32mLevel cleared!\033[0m  N next level | R restart | Q menu");
        }
    } else {
        console_write("WASD move | Space/Enter open | F flag | Q menu");
    }
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
