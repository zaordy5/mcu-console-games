#include "view.h"

/** Fixed layout for all three fields in a 64 by 20 terminal. */
enum {
    COLUMN_ROW = 3, FIELD_ROW = 4, FIELD_COLUMN = 4, CELL_WIDTH = 3,
    PANEL_COLUMN = 43, PANEL_WIDTH = 21, FOOTER_ROW = 18,
    LAST_COLUMN = 63, LAST_ROW = 20, FIRST_REVEAL_MS = 160
};

/** RGB roles shared by the game, pause, and setup screens. */
typedef enum {
    INK, MUTED, PINK, PEACH, LAVENDER, MINT, MELON, ICE, CREAM, ROSE,
    BASE, FIELD, TILE, PREVIEW, FLASH, CURSOR, BLAST, SUNSET_VIOLET,
    SUNSET_PINK, SUNSET_PEACH, NO_COLOR
} Color;

/** Color channels for ANSI SGR in TermSharp. */
static const char *const rgb[] = {
    [INK] = "255;238;250", [MUTED] = "205;170;209", [PINK] = "255;133;199",
    [PEACH] = "255;203;151", [LAVENDER] = "217;190;255", [MINT] = "162;233;218",
    [MELON] = "255;168;163", [ICE] = "194;217;255", [CREAM] = "255;229;183",
    [ROSE] = "253;164;229", [BASE] = "24;15;39", [FIELD] = "36;22;56",
    [TILE] = "89;39;81", [PREVIEW] = "122;52;105", [FLASH] = "106;48;94",
    [CURSOR] = "255;119;191", [BLAST] = "167;38;89", [SUNSET_VIOLET] = "112;63;162",
    [SUNSET_PINK] = "220;84;155", [SUNSET_PEACH] = "255;172;123"
};

/** One foreground per revealed clue from one through eight. */
static const Color number_colors[] = { PEACH, PINK, LAVENDER, MINT, MELON, ICE, CREAM, ROSE };

/** Appearance bits packed above each cell's ASCII symbol. */
enum { SELECTED = 0x100, TARGET = 0x200, NEW_AREA = 0x400 };

/** Up to eight covered neighbors of the current clue, in board order. */
typedef struct {
    uint8_t targets[8]; /**< Indices of covered, unflagged neighbors. */
    unsigned count;     /**< Number of such neighbors. */
    unsigned flags;     /**< Already flagged neighbors. */
    unsigned clue;      /**< Revealed number under the cursor. */
} Chord;

/** Cached screen details used to refresh only changed parts. */
static struct {
    uint16_t cells[MINES_MAX_CELLS]; /**< Last visible symbol and appearance. */
    uint8_t side;                    /**< Zero when the board must be drawn in full. */
    uint8_t cursor;                  /**< Previously painted cursor. */
    unsigned footer;                 /**< Flags governing available footer keys. */
    MinesScreen screen;              /**< Previously painted controller screen. */
    unsigned seconds;                /**< Last clock value printed. */
    int left;                        /**< Last remaining-mine count printed. */
    unsigned opened;                 /**< Last progress count printed. */
    unsigned clue, flags, targets;   /**< Last chord feedback contents. */
    unsigned flash_ms;               /**< Reveal feedback time still visible. */
    bool logical;                   /**< Previously painted mode. */
} painted;

/** Styles active within one draw operation. */
static Color foreground = NO_COLOR, background = NO_COLOR;

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

/** Emit only color channels that changed since the previous write. */
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

/** Put subsequent non-Minesweeper output back in the terminal's normal colors. */
static void reset_style(void)
{
    console_write("\033[0m");
    foreground = NO_COLOR;
    background = NO_COLOR;
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

/** Paint a run of background cells without touching the last terminal column. */
static void blanks(unsigned count)
{
    while (count > 0) {
        static const char spaces[LAST_COLUMN + 1] =
            "                     " "                     " "                     ";
        unsigned chunk = count < LAST_COLUMN ? count : LAST_COLUMN;
        console_write(spaces + LAST_COLUMN - chunk);
        count -= chunk;
    }
}

/** Set up the violet canvas and a three-color sunset rule. */
static void canvas(void)
{
    console_write("\033[H\033[J");
    style(INK, BASE);
    for (unsigned row = 1; row <= LAST_ROW; ++row) {
        move_to(row, 1);
        blanks(LAST_COLUMN);
    }
    move_to(2, 1);
    style(INK, SUNSET_VIOLET);
    blanks(21);
    style(INK, SUNSET_PINK);
    blanks(21);
    style(INK, SUNSET_PEACH);
    blanks(21);
}

/** Position the cursor clear of the field and keys before leaving the renderer. */
static void finish(void)
{
    move_to(LAST_ROW, LAST_COLUMN);
    reset_style();
}

void mines_view_invalidate(void)
{
    painted.side = 0;
    painted.flash_ms = 0;
}

void mines_view_first_reveal(void)
{
    painted.flash_ms = FIRST_REVEAL_MS;
}

bool mines_view_tick(uint32_t elapsed_ms)
{
    if (painted.flash_ms == 0) {
        return false;
    }
    if (elapsed_ms < painted.flash_ms) {
        painted.flash_ms -= elapsed_ms;
        return false;
    }
    painted.flash_ms = 0;
    return true;
}

/** Show the mode, selected difficulty, and campaign level. */
static void title(const MinesRun *run)
{
    move_to(1, 1);
    style(PINK, BASE);
    console_write(" MINESWEEPER ");
    move_to(1, 38);
    style(PEACH, BASE);
    blanks(26);
    move_to(1, 38);
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
}

/** Write an A1-style board position. */
static void coordinate(uint8_t index, uint8_t side)
{
    char letter[] = { (char)('A' + index % side), '\0' };
    console_write(letter);
    write_number(index / side + 1);
}

/** Write the clock in minutes and seconds, keeping short times fixed-width. */
static void clock_time(unsigned seconds)
{
    if (seconds / 60 < 10) console_write("0");
    write_number(seconds / 60);
    console_write(":");
    if (seconds % 60 < 10) console_write("0");
    write_number(seconds % 60);
}

/** Clear a line inside the fixed-width side panel. */
static void panel_line(unsigned row, Color color)
{
    move_to(row, PANEL_COLUMN);
    style(color, BASE);
    blanks(PANEL_WIDTH);
    move_to(row, PANEL_COLUMN);
}

/** Refresh only values that changed in the side panel. */
static void stats(const MinesRun *run, bool full)
{
    const MinesBoard *board = &run->board;
    unsigned seconds = run->played_ms / 1000;
    unsigned total_safe = board->side * board->side - board->mine_count;
    unsigned opened = total_safe - board->remaining;
    int left = (int)board->mine_count - board->flagged;

    if (full || painted.left != left) {
        panel_line(4, PEACH);
        console_write("Mines left ");
        if (left < 0) console_write("-");
        write_number((unsigned)(left < 0 ? -left : left));
        console_write("/");
        write_number(board->mine_count);
        painted.left = left;
    }
    if (full || painted.seconds != seconds) {
        panel_line(5, INK);
        console_write("Time ");
        clock_time(seconds);
        painted.seconds = seconds;
    }
    if (full || painted.opened != opened) {
        panel_line(6, INK);
        console_write("Opened ");
        write_number(opened);
        console_write("/");
        write_number(total_safe);
        painted.opened = opened;
    }
    if (full || painted.cursor != run->cursor) {
        panel_line(7, PINK);
        console_write("Cell ");
        coordinate(run->cursor, board->side);
    }
}

void mines_view_hud(const MinesRun *run)
{
    stats(run, false);
    finish();
}

/** Build a preview from visible clues and flags only. */
static void chord_at_cursor(const MinesRun *run, MinesScreen screen, Chord *hint)
{
    hint->count = 0;
    hint->flags = 0;
    hint->clue = 0;
    const MinesBoard *board = &run->board;
    if (screen != MINES_PLAY || !(board->cells[run->cursor] & CELL_OPEN)) {
        return;
    }
    hint->clue = mines_neighbors(board, run->cursor);
    if (hint->clue == 0) {
        return;
    }

    uint8_t neighbors[8];
    unsigned count = mines_adjacent(board, run->cursor, neighbors);
    for (unsigned i = 0; i < count; ++i) {
        uint8_t cell = board->cells[neighbors[i]];
        if (cell & CELL_FLAG) {
            ++hint->flags;
        } else if (!(cell & CELL_OPEN)) {
            hint->targets[hint->count++] = neighbors[i];
        }
    }
    if (hint->flags != hint->clue) {
        hint->count = 0;
    }
}

/** Paint the interaction hint or the result while the field stays in view. */
static void panel(const MinesRun *run, MinesScreen screen, bool full, const Chord *hint)
{
    if (!full && screen == painted.screen && painted.clue == hint->clue &&
        painted.flags == hint->flags && painted.targets == hint->count) {
        return;
    }
    painted.clue = hint->clue;
    painted.flags = hint->flags;
    painted.targets = hint->count;

    panel_line(9, PINK);
    if (screen == MINES_LOSS) {
        console_write("Mine hit! ");
        coordinate(run->board.hit, run->board.side);
    } else if (screen == MINES_WIN) {
        style(MINT, BASE);
        if (run->mode == MINES_CAMPAIGN) {
            console_write(run->level + 1 == MINES_LEVELS ? "Campaign complete!" : "Level cleared!");
        } else {
            console_write("Field cleared!");
        }
    } else if (screen == MINES_SEARCH) {
        console_write("Checking layouts...");
    } else if (hint->clue != 0) {
        console_write("Flags nearby ");
        write_number(hint->flags);
        console_write("/");
        write_number(hint->clue);
    }

    panel_line(10, MUTED);
    if (screen == MINES_LOSS) {
        console_write("! blast | X error");
    } else if (hint->count > 0) {
        style(PEACH, BASE);
        console_write("Space opens ");
        write_number(hint->count);
    }
}

/** Choose a visible cell mark without revealing unopened mines during play. */
static char cell_symbol(const MinesBoard *board, uint8_t index, MinesScreen screen)
{
    uint8_t cell = board->cells[index];
    if (screen == MINES_LOSS && index == board->hit) return '!';
    if (cell & CELL_FLAG) return screen == MINES_LOSS && !(cell & CELL_MINE) ? 'X' : 'F';
    if (screen == MINES_LOSS && (cell & CELL_MINE)) return '*';
    if (cell & CELL_OPEN) {
        uint8_t nearby = mines_neighbors(board, index);
        return nearby ? (char)('0' + nearby) : ' ';
    }
    return cell & CELL_QUESTION ? '?' : '.';
}

/** Render a clue, covered tile, or selected bracket pair within three columns. */
static void paint_cell(uint16_t cell)
{
    char symbol = (char)(cell & 127);
    bool selected = (cell & SELECTED) != 0;
    char label[] = { selected ? '[' : ' ', symbol, selected ? ']' : ' ', '\0' };

    if (symbol == '!') {
        style(INK, BLAST);
    } else if (selected) {
        style(BASE, CURSOR);
    } else if (symbol == '.' || symbol == 'F' || symbol == '?') {
        style(symbol == 'F' ? PEACH : symbol == '?' ? LAVENDER : MUTED, cell & TARGET ? PREVIEW : TILE);
        label[2] = '\0';
        console_write(label);
        style(foreground, FIELD);
        console_write(" ");
        return;
    } else {
        Color fg = symbol >= '1' && symbol <= '8' ? number_colors[symbol - '1'] :
                   symbol == 'X' || symbol == '*' ? MELON : INK;
        style(fg, cell & NEW_AREA ? FLASH : FIELD);
    }
    console_write(label);
}

/** Paint a column letter over its tile. */
static void column_label(unsigned column, bool selected)
{
    move_to(COLUMN_ROW, FIELD_COLUMN + CELL_WIDTH * column + 1);
    style(selected ? PINK : MUTED, BASE);
    char label[] = { (char)('A' + column), '\0' };
    console_write(label);
}

/** Paint a row number directly left of the tiles. */
static void row_label(unsigned row, bool selected)
{
    move_to(FIELD_ROW + row, 1);
    style(selected ? PINK : MUTED, BASE);
    if (row < 9) console_write(" ");
    write_number(row + 1);
    console_write(" ");
}

/** Refresh the old and new labels when the cursor changes rows or columns. */
static void axes(const MinesRun *run, bool full)
{
    unsigned side = run->board.side;
    unsigned x = run->cursor % side;
    unsigned y = run->cursor / side;
    if (full) {
        for (unsigned i = 0; i < side; ++i) {
            column_label(i, i == x);
            row_label(i, i == y);
        }
        return;
    }

    unsigned previous_x = painted.cursor % side;
    unsigned previous_y = painted.cursor / side;
    if (previous_x != x) {
        column_label(previous_x, false);
        column_label(x, true);
    }
    if (previous_y != y) {
        row_label(previous_y, false);
        row_label(y, true);
    }
}

/** Update only tiles whose contents, focus, or visual feedback changed. */
static void paint_field(const MinesRun *run, MinesScreen screen, bool full, const Chord *hint)
{
    uint8_t side = run->board.side;
    unsigned target = 0;
    for (uint8_t y = 0; y < side; ++y) {
        unsigned next_column = 0;
        for (uint8_t x = 0; x < side; ++x) {
            uint8_t index = y * side + x;
            uint16_t tile = (uint16_t)cell_symbol(&run->board, index, screen);
            if (index == run->cursor) tile |= SELECTED;
            if (target < hint->count && index == hint->targets[target]) {
                tile |= TARGET;
                ++target;
            }
            if (painted.flash_ms > 0 && screen == MINES_PLAY && (run->board.cells[index] & CELL_OPEN)) {
                tile |= NEW_AREA;
            }
            if (!full && tile == painted.cells[index]) continue;

            unsigned column = FIELD_COLUMN + CELL_WIDTH * x;
            if (next_column != column) move_to(FIELD_ROW + y, column);
            paint_cell(tile);
            painted.cells[index] = tile;
            next_column = column + CELL_WIDTH;
        }
    }
}

/** Write the exact code accepted by the game and host tools. */
static void print_layout(const MinesRun *run)
{
    console_write(" Layout ");
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

/** Draw the footer separator and current controls in their three reserved rows. */
static void controls(const MinesRun *run, MinesScreen screen, bool can_resume, bool search_failed)
{
    for (unsigned row = FOOTER_ROW; row <= LAST_ROW; ++row) {
        move_to(row, 1);
        style(INK, BASE);
        blanks(LAST_COLUMN);
    }
    move_to(FOOTER_ROW, 1);
    style(PEACH, BASE);
    if (screen == MINES_SEARCH) {
        console_write(" Q menu");
        return;
    }
    if (screen == MINES_LOSS || screen == MINES_WIN) {
        if (screen == MINES_WIN && run->mode == MINES_CAMPAIGN && run->level + 1 < MINES_LEVELS) {
            console_write(" N next level   ");
        }
        console_write(run->mode == MINES_SHARED ? " B replay   R campaign   Q menu" :
                                                   " B replay   R restart   Q menu");
        move_to(FOOTER_ROW + 1, 1);
        style(MUTED, BASE);
        print_layout(run);
        return;
    }

    console_write(" Arrows/WASD move   Space open   F flag   ? mark");
    move_to(FOOTER_ROW + 1, 1);
    console_write(" P pause   Q menu");
    if (!run->board.generated) {
        console_write("   C custom   L load");
        if (mines_board_logic_allowed(&run->board)) console_write("   G Logic");
        if (can_resume) console_write("   U continue");
        if (search_failed) {
            move_to(LAST_ROW, 1);
            style(PINK, BASE);
            console_write(" Layout search failed. Retry Space or switch mode with G.");
        }
    }
}

void mines_view_board(const MinesRun *run, MinesScreen screen, bool can_resume, bool search_failed)
{
    bool full = painted.side != run->board.side;
    unsigned footer = (run->board.generated ? 1u : 0u) |
                      (can_resume ? 2u : 0u) | (search_failed ? 4u : 0u);
    Chord hint;
    chord_at_cursor(run, screen, &hint);
    if (full) canvas();
    if (full || painted.logical != run->logical) title(run);
    stats(run, full);
    axes(run, full);
    paint_field(run, screen, full, &hint);
    panel(run, screen, full, &hint);
    if (full || screen != painted.screen || footer != painted.footer) {
        controls(run, screen, can_resume, search_failed);
    }
    painted.side = run->board.side;
    painted.cursor = run->cursor;
    painted.screen = screen;
    painted.footer = footer;
    painted.logical = run->logical;
    finish();
}

/** Open a matching sunset-themed pause or setup page. */
static void page(const char *heading)
{
    mines_view_invalidate();
    canvas();
    move_to(1, 2);
    style(PINK, BASE);
    console_write(heading);
    move_to(4, 2);
    style(INK, BASE);
}

void mines_view_pause(const MinesRun *run)
{
    page("Minesweeper paused");
    console_write("Time ");
    clock_time(run->played_ms / 1000);
    move_to(6, 2);
    style(PEACH, BASE);
    console_write(run->board.generated ? "P resume   Q save and return to menu" : "P resume   Q menu");
    finish();
}

void mines_view_form(MinesScreen screen, unsigned side, const char *input, bool invalid)
{
    if (screen == MINES_SIZE_FORM) {
        page("Minesweeper  Custom field");
        console_write("Choose size");
        move_to(5, 2);
        console_write("1. 8 x 8   2. 10 x 10   3. 12 x 12");
        move_to(7, 2);
        style(PEACH, BASE);
        console_write("Q cancel");
        finish();
        return;
    }

    if (screen == MINES_COUNT_FORM) {
        page("Minesweeper  Custom field");
        console_write("Enter mine count (1-");
        write_number(side * side - MINES_SAFE_AREA);
        console_write(")");
    } else {
        page("Minesweeper  Load layout");
        console_write("Enter code from a result screen");
    }
    move_to(6, 2);
    style(PINK, BASE);
    console_write("> ");
    console_write(input);
    move_to(8, 2);
    style(PEACH, BASE);
    console_write("Enter confirm   Backspace erase   Q cancel");
    if (invalid) {
        move_to(9, 2);
        style(MELON, BASE);
        console_write(screen == MINES_COUNT_FORM ? "Mine count is outside the allowed range." :
                                                  "Invalid layout code.");
    }
    finish();
}
