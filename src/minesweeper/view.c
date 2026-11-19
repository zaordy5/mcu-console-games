#include "view.h"

/** Terminal geometry of the board. */
enum { HUD_ROW = 2, COLUMN_ROW = 3, FIELD_ROW = 4, FIELD_COLUMN = 4, CELL_WIDTH = 3 };

/** Marker packed into a printable cell's upper bit to track cursor changes. */
enum { SELECTED = 0x80 };

/** Cached characters and cursor highlight for the field currently on screen. */
static uint8_t visible[MINES_MAX_CELLS];

/** Width of the painted field, or zero when no field is visible. */
static uint8_t painted_side;

/** Distinct clue colors on the dark field. */
static const char *const number_colors[] = {
    "\033[94;40m", "\033[92;40m", "\033[91;40m", "\033[95;40m",
    "\033[93;40m", "\033[96;40m", "\033[97;40m", "\033[90;40m"
};

/** Write an unsigned decimal value without a hosted C library. */
static void write_number(uint32_t value)
{
    char digits[11];
    unsigned i = sizeof digits - 1;
    digits[i] = '\0';
    do {
        digits[--i] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    console_write(digits + i);
}

/** Move the terminal cursor to a one-based screen position. */
static void move_to(unsigned row, unsigned column)
{
    console_write("\033[");
    write_number(row);
    console_write(";");
    write_number(column);
    console_write("H");
}

void mines_view_invalidate(void)
{
    painted_side = 0;
}

/** Clear the screen at a page transition and set a title. */
static void page(const char *title)
{
    mines_view_invalidate();
    console_write("\033[H\033[J\033[1m");
    console_write(title);
    console_write("\033[0m\r\n");
}

/** Show the mode, selected difficulty, and campaign level. */
static void title(const MinesRun *run)
{
    console_write("\033[H\033[1mMinesweeper\033[0m  ");
    if (run->mode == MINES_CAMPAIGN) {
        console_write("L");
        write_number(run->level + 1);
        console_write("/3  ");
        switch (run->difficulty) {
        case GAME_EASY: console_write("Easy"); break;
        case GAME_HARD: console_write("Hard"); break;
        default: console_write("Normal"); break;
        }
    } else if (run->mode == MINES_CUSTOM) {
        console_write("Custom");
    } else {
        console_write("Shared layout");
    }
    if (run->mode != MINES_SHARED) {
        console_write(run->logical ? "  Logic" : "  Classic");
    }
    console_write("\033[K");
}

void mines_view_hud(const MinesRun *run)
{
    move_to(HUD_ROW, 1);
    console_write("Mines ");
    write_number(run->board.mine_count);
    console_write("  Left ");
    int left = run->board.mine_count - run->board.flagged;
    if (left < 0) {
        console_write("-");
        left = -left;
    }
    write_number((unsigned)left);
    console_write("  Time ");
    write_number(run->played_ms / 1000);
    console_write("s  Cell ");
    char column[] = {(char)('A' + run->cursor % run->board.side), '\0'};
    console_write(column);
    write_number(run->cursor / run->board.side + 1);
    console_write("\033[K");
}

/** Choose a cell symbol from public game state. */
static char cell_symbol(const MinesBoard *board, uint8_t index, MinesScreen screen)
{
    uint8_t cell = board->cells[index];
    if (screen == MINES_LOSS && index == board->hit) {
        return '!';
    }
    if (cell & CELL_FLAG) {
        return screen == MINES_LOSS && !(cell & CELL_MINE) ? 'X' : 'F';
    }
    if (screen == MINES_LOSS && (cell & CELL_MINE)) {
        return '*';
    }
    if (cell & CELL_OPEN) {
        uint8_t nearby = mines_neighbors(board, index);
        return nearby ? (char)('0' + nearby) : ' ';
    }
    return (cell & CELL_QUESTION) ? '?' : '.';
}

/** Paint a width-three cell, emphasizing the cursor without relying on color alone. */
static void paint_cell(uint8_t visible_cell)
{
    bool selected = (visible_cell & SELECTED) != 0;
    char symbol = (char)(visible_cell & ~SELECTED);
    const char *color = "\033[37;40m";
    if (symbol == '!') {
        color = "\033[97;41m";
    } else if (selected) {
        color = "\033[97;44m";
    } else if (symbol >= '1' && symbol <= '8') {
        color = number_colors[symbol - '1'];
    } else if (symbol == 'F') {
        color = "\033[93;40m";
    } else if (symbol == '?') {
        color = "\033[95;40m";
    } else if (symbol == 'X' || symbol == '*') {
        color = "\033[91;40m";
    }

    char label[] = {selected ? '[' : ' ', symbol, selected ? ']' : ' ', '\0'};
    console_write(color);
    console_write(label);
    console_write("\033[0m");
}

/** Write column letters aligned with their cells. */
static void paint_columns(uint8_t side)
{
    move_to(COLUMN_ROW, 1);
    console_write("\033[96;40m   ");
    for (uint8_t x = 0; x < side; ++x) {
        char label[] = {' ', (char)('A' + x), ' ', '\0'};
        console_write(label);
    }
    console_write("\033[0m");
}

/** Update changed cells while keeping board labels fixed. */
static void paint_field(const MinesRun *run, MinesScreen screen, bool full)
{
    uint8_t side = run->board.side;
    if (full) {
        paint_columns(side);
    }
    for (uint8_t y = 0; y < side; ++y) {
        unsigned row = FIELD_ROW + y;
        unsigned next_column = 0;
        if (full) {
            move_to(row, 1);
            console_write("\033[96;40m");
            console_write(y < 9 ? " " : "");
            write_number(y + 1);
            console_write(" ");
            next_column = FIELD_COLUMN;
        }
        for (uint8_t x = 0; x < side; ++x) {
            uint8_t i = y * side + x;
            uint8_t cell = (uint8_t)cell_symbol(&run->board, i, screen);
            if (i == run->cursor) {
                cell |= SELECTED;
            }
            if (!full && cell == visible[i]) {
                continue;
            }

            unsigned column = FIELD_COLUMN + CELL_WIDTH * x;
            if (next_column != column) {
                move_to(row, column);
            }
            paint_cell(cell);
            visible[i] = cell;
            next_column = column + CELL_WIDTH;
        }
    }
}

/** Write the exact code accepted by the game and host tools. */
static void print_layout(const MinesRun *run)
{
    console_write("\r\nLayout ");
    write_number(run->board.side);
    console_write("/");
    write_number(run->board.mine_count);
    console_write("/");
    write_number(run->first);
    console_write("/");
    write_number(run->seed);
    console_write("/");
    write_number(run->layout);
}

/** Show the controls that apply to the current stage of the game. */
static void controls(const MinesRun *run, MinesScreen screen, bool can_resume, bool search_failed)
{
    move_to(FIELD_ROW + run->board.side, 1);
    console_write("\033[J");
    if (screen == MINES_SEARCH) {
        console_write("Checking layouts... Q menu");
    } else if (screen == MINES_LOSS || screen == MINES_WIN) {
        if (screen == MINES_LOSS) {
            console_write("\033[91mMine hit!\033[0m  ! blast  X wrong flag");
        } else if (run->mode != MINES_CAMPAIGN) {
            console_write("\033[92mField cleared!\033[0m");
        } else if (run->level + 1 == MINES_LEVELS) {
            console_write("\033[92mCampaign complete!\033[0m");
        } else {
            console_write("\033[92mLevel cleared!\033[0m  N next level");
        }
        console_write(run->mode == MINES_SHARED ? "\r\nB replay | R campaign | Q menu" :
                                                   "\r\nB replay | R restart | Q menu");
        print_layout(run);
    } else {
        console_write("Arrows/WASD move | Space/Enter open | F flag | ? mark\r\nP pause | Q menu");
        if (!run->board.generated) {
            console_write("\r\nC custom | L load");
            if (mines_board_logic_allowed(&run->board)) {
                console_write(" | G Logic");
            }
            if (can_resume) {
                console_write(" | U continue");
            }
            if (search_failed) {
                console_write("\r\nLayout search failed. Retry Space or switch mode with G.");
            }
        }
    }
}

void mines_view_board(const MinesRun *run, MinesScreen screen, bool can_resume, bool search_failed)
{
    bool full = painted_side != run->board.side;
    if (full) {
        console_write("\033[H\033[J");
    }
    title(run);
    mines_view_hud(run);
    paint_field(run, screen, full);
    controls(run, screen, can_resume, search_failed);
    painted_side = run->board.side;
}

void mines_view_pause(const MinesRun *run)
{
    page("Minesweeper paused");
    console_write("Time ");
    write_number(run->played_ms / 1000);
    console_write(run->board.generated ? "s\r\nP resume | Q save and return to menu" :
                                         "s\r\nP resume | Q menu");
}

void mines_view_form(MinesScreen screen, unsigned side, const char *input, bool invalid)
{
    if (screen == MINES_SIZE_FORM) {
        page("Minesweeper  Custom field");
        console_write("Choose size\r\n1. 8 x 8   2. 10 x 10   3. 12 x 12\r\nQ cancel");
        return;
    }

    if (screen == MINES_COUNT_FORM) {
        page("Minesweeper  Custom field");
        console_write("Enter mine count (1-");
        write_number(side * side - MINES_SAFE_AREA);
        console_write(")\r\n");
    } else {
        page("Minesweeper  Load layout");
        console_write("Enter code from a result screen\r\n");
    }
    console_write("> ");
    console_write(input);
    console_write("\r\nEnter confirm | Backspace erase | Q cancel");
    if (invalid) {
        console_write(screen == MINES_COUNT_FORM ? "\r\nMine count is outside the allowed range." :
                                                  "\r\nInvalid layout code.");
    }
}
