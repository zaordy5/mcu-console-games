#include "board.h"

/** Advance a nonzero xorshift32 generator. */
static uint32_t random_next(uint32_t state)
{
    state ^= state << 13;
    state ^= state >> 17;
    return state ^ (state << 5);
}

bool mines_board_valid(unsigned side, unsigned mine_count)
{
    return side >= MINES_MIN_SIDE && side <= MINES_MAX_SIDE && side % 2 == 0 &&
           mine_count > 0 && mine_count <= side * side - MINES_SAFE_AREA;
}

bool mines_board_logic_allowed(const MinesBoard *board)
{
    return board->mine_count <= (board->side * board->side) / 4;
}

void mines_board_reset(MinesBoard *board, uint8_t side, uint8_t mine_count)
{
    board->side = side;
    board->mine_count = mine_count;
    board->remaining = side * side - mine_count;
    board->flagged = 0;
    board->hit = MINES_NO_HIT;
    board->generated = false;
    for (unsigned i = 0; i < side * side; ++i) {
        board->cells[i] = 0;
    }
}

void mines_board_generate(MinesBoard *board, uint8_t first, uint32_t seed, MinesLayout layout)
{
    uint8_t candidates[MINES_MAX_CELLS];
    unsigned count = 0;
    int side = board->side;
    int first_x = first % side;
    int first_y = first / side;
    bool vertical = layout != MINES_SCATTERED && (seed & 1) != 0;
    bool reverse = layout != MINES_SCATTERED && (seed & 2) != 0;
    int first_depth = vertical ? first_x : first_y;
    bool far_end = layout != MINES_SCATTERED && first_depth < side / 2;

    for (int i = 0; i < side * side; ++i) {
        board->cells[i] &= (uint8_t)~CELL_MINE;
    }
    for (int depth = 0; depth < side; ++depth) {
        int major = far_end ? side - 1 - depth : depth;
        for (int offset = 0; offset < side; ++offset) {
            int minor = reverse ? side - 1 - offset : offset;
            int x = vertical ? major : minor;
            int y = vertical ? minor : major;
            int dx = x - first_x;
            int dy = y - first_y;
            if (dx < -1 || dx > 1 || dy < -1 || dy > 1) {
                candidates[count++] = y * side + x;
            }
        }
    }
    if (seed == 0) {
        seed = 1;
    }
    if (layout == MINES_BANDED && board->mine_count > 0 && board->mine_count < count) {
        // Shift one mine near the boundary to vary clues on fallback boards.
        unsigned boundary = board->mine_count;
        seed = random_next(seed);
        unsigned width = boundary < (unsigned)side ? boundary : (unsigned)side;
        unsigned from = boundary - 1 - seed % width;
        seed = random_next(seed);
        unsigned available = count - boundary;
        unsigned reach = available < (unsigned)side ? available : (unsigned)side;
        unsigned to = boundary + seed % reach;
        uint8_t cell = candidates[from];
        candidates[from] = candidates[to];
        candidates[to] = cell;
    }
    for (unsigned i = 0; i < board->mine_count; ++i) {
        if (layout != MINES_SCATTERED) {
            board->cells[candidates[i]] |= CELL_MINE;
        } else {
            seed = random_next(seed);
            unsigned picked = seed % count;
            board->cells[candidates[picked]] |= CELL_MINE;
            candidates[picked] = candidates[--count];
        }
    }
    board->generated = true;
}

/** Collect valid neighbors of a cell, including diagonals. */
static unsigned adjacent(const MinesBoard *board, uint8_t index, uint8_t neighbors[8])
{
    int x = index % board->side;
    int y = index / board->side;
    unsigned count = 0;

    for (int row = y - 1; row <= y + 1; ++row) {
        for (int col = x - 1; col <= x + 1; ++col) {
            if (row >= 0 && row < board->side && col >= 0 && col < board->side &&
                (row != y || col != x)) {
                neighbors[count++] = row * board->side + col;
            }
        }
    }
    return count;
}

/** Count neighbors with the supplied flag set. */
static unsigned count_matching(const MinesBoard *board, const uint8_t *neighbors,
                               unsigned count, uint8_t flag)
{
    unsigned total = 0;

    for (unsigned i = 0; i < count; ++i) {
        if (board->cells[neighbors[i]] & flag) {
            ++total;
        }
    }
    return total;
}

uint8_t mines_neighbors(const MinesBoard *board, uint8_t index)
{
    uint8_t neighbors[8];
    unsigned count = adjacent(board, index, neighbors);
    return count_matching(board, neighbors, count, CELL_MINE);
}

bool mines_flag(MinesBoard *board, uint8_t index)
{
    if (board->cells[index] & CELL_OPEN) {
        return false;
    }
    if (board->cells[index] & CELL_FLAG) {
        board->cells[index] &= (uint8_t)~CELL_FLAG;
        --board->flagged;
    } else {
        board->cells[index] &= (uint8_t)~CELL_QUESTION;
        board->cells[index] |= CELL_FLAG;
        ++board->flagged;
    }
    return true;
}

bool mines_question(MinesBoard *board, uint8_t index)
{
    if (board->cells[index] & CELL_OPEN) {
        return false;
    }
    if (board->cells[index] & CELL_FLAG) {
        mines_flag(board, index);
    }
    board->cells[index] ^= CELL_QUESTION;
    return true;
}

/** Reveal a safe cell and queue it once for the iterative flood. */
static void enqueue(MinesBoard *board, uint8_t index, uint8_t *queue, unsigned *tail)
{
    board->cells[index] = CELL_OPEN;
    --board->remaining;
    queue[(*tail)++] = index;
}

/** Reveal one unflagged, closed cell and expand its empty region. */
static void reveal(MinesBoard *board, uint8_t index)
{
    if (board->cells[index] & CELL_MINE) {
        board->hit = index;
        return;
    }

    uint8_t queue[MINES_MAX_CELLS];
    unsigned tail = 0;
    enqueue(board, index, queue, &tail);
    for (unsigned head = 0; head < tail; ++head) {
        uint8_t cell = queue[head];
        if (mines_neighbors(board, cell)) {
            continue;
        }
        uint8_t neighbors[8];
        unsigned count = adjacent(board, cell, neighbors);
        for (unsigned i = 0; i < count; ++i) {
            uint8_t neighbor = neighbors[i];
            if (!(board->cells[neighbor] & (CELL_OPEN | CELL_FLAG | CELL_MINE))) {
                enqueue(board, neighbor, queue, &tail);
            }
        }
    }
}

MinesResult mines_open(MinesBoard *board, uint8_t index)
{
    if (!board->generated || board->hit != MINES_NO_HIT || board->remaining == 0 ||
        (board->cells[index] & CELL_FLAG)) {
        return MINES_NO_CHANGE;
    }

    unsigned before = board->remaining;
    if (!(board->cells[index] & CELL_OPEN)) {
        reveal(board, index);
    } else {
        unsigned number = mines_neighbors(board, index);
        uint8_t neighbors[8];
        unsigned count = adjacent(board, index, neighbors);
        if (number == 0 || count_matching(board, neighbors, count, CELL_FLAG) != number) {
            return MINES_NO_CHANGE;
        }
        for (unsigned i = 0; i < count && board->hit == MINES_NO_HIT; ++i) {
            uint8_t cell = neighbors[i];
            if (!(board->cells[cell] & (CELL_OPEN | CELL_FLAG))) {
                reveal(board, cell);
            }
        }
    }
    if (board->hit != MINES_NO_HIT) return MINES_LOST;
    if (board->remaining == 0) return MINES_WON;
    return board->remaining < before ? MINES_OPENED : MINES_NO_CHANGE;
}

/** Apply the two single-number deductions around one open cell. */
static bool deduce(MinesBoard *board, uint8_t cell)
{
    uint8_t neighbors[8];
    unsigned count = adjacent(board, cell, neighbors);
    unsigned flagged = count_matching(board, neighbors, count, CELL_FLAG);
    unsigned opened = count_matching(board, neighbors, count, CELL_OPEN);
    unsigned unknown = count - flagged - opened;
    unsigned number = mines_neighbors(board, cell);

    if (unknown == 0) {
        return false;
    }
    if (flagged == number) {
        for (unsigned i = 0; i < count; ++i) {
            uint8_t next = neighbors[i];
            if (!(board->cells[next] & (CELL_FLAG | CELL_OPEN))) {
                reveal(board, next);
            }
        }
        return true;
    }
    if (flagged + unknown == number) {
        for (unsigned i = 0; i < count; ++i) {
            uint8_t next = neighbors[i];
            if (!(board->cells[next] & (CELL_FLAG | CELL_OPEN))) {
                mines_flag(board, next);
            }
        }
        return true;
    }
    return false;
}

/** Count hidden, unflagged neighbors and return the clue's remaining mine count. */
static unsigned unknown_neighbors(const MinesBoard *board, uint8_t cell,
                                  uint8_t unknown[8], unsigned *mines)
{
    uint8_t neighbors[8];
    unsigned count = adjacent(board, cell, neighbors);
    unsigned hidden = 0;
    *mines = mines_neighbors(board, cell) - count_matching(board, neighbors, count, CELL_FLAG);
    for (unsigned i = 0; i < count; ++i) {
        if (!(board->cells[neighbors[i]] & (CELL_OPEN | CELL_FLAG))) {
            unknown[hidden++] = neighbors[i];
        }
    }
    return hidden;
}

/** Derive safe cells or mines from the difference of two overlapping clues. */
static bool deduce_difference(MinesBoard *board, const uint8_t small[8], unsigned small_count,
                              unsigned small_mines, uint8_t other)
{
    uint8_t large[8], difference[8];
    unsigned large_mines;
    unsigned large_count = unknown_neighbors(board, other, large, &large_mines);
    if (large_count <= small_count || large_mines < small_mines) {
        return false;
    }

    unsigned different = 0;
    for (unsigned i = 0; i < large_count; ++i) {
        unsigned j = 0;
        while (j < small_count && small[j] != large[i]) {
            ++j;
        }
        if (j == small_count) {
            difference[different++] = large[i];
        }
    }
    if (different != large_count - small_count) {
        return false;
    }

    unsigned mines = large_mines - small_mines;
    if (mines == 0) {
        for (unsigned i = 0; i < different; ++i) {
            if (!(board->cells[difference[i]] & CELL_OPEN)) {
                reveal(board, difference[i]);
            }
        }
    } else if (mines == different) {
        for (unsigned i = 0; i < different; ++i) {
            mines_flag(board, difference[i]);
        }
    } else {
        return false;
    }
    return true;
}

/** Try comparing nearby open clues after single-clue deductions stall. */
static bool deduce_subsets(MinesBoard *board)
{
    unsigned cells = board->side * board->side;
    for (unsigned i = 0; i < cells; ++i) {
        if (!(board->cells[i] & CELL_OPEN)) continue;
        uint8_t small[8];
        unsigned mines;
        unsigned count = unknown_neighbors(board, i, small, &mines);
        if (count == 0) continue;

        int x = i % board->side;
        int y = i / board->side;
        for (int row = y - 2; row <= y + 2; ++row) {
            for (int col = x - 2; col <= x + 2; ++col) {
                if (row < 0 || row >= board->side || col < 0 || col >= board->side) continue;
                uint8_t other = row * board->side + col;
                if ((board->cells[other] & CELL_OPEN) &&
                    deduce_difference(board, small, count, mines, other)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool mines_board_solvable(const MinesBoard *board, uint8_t first)
{
    if (!board->generated) {
        return false;
    }
    MinesBoard trial;
    mines_board_reset(&trial, board->side, board->mine_count);
    unsigned cells = board->side * board->side;
    for (unsigned i = 0; i < cells; ++i) {
        trial.cells[i] = board->cells[i] & CELL_MINE;
    }
    trial.generated = true;
    MinesResult first_result = mines_open(&trial, first);
    if (first_result == MINES_LOST) return false;

    bool changed = true;
    while (changed && trial.remaining > 0 && trial.hit == MINES_NO_HIT) {
        changed = false;
        for (unsigned i = 0; i < cells; ++i) {
            if (trial.flagged == trial.mine_count) {
                if (!(trial.cells[i] & (CELL_FLAG | CELL_OPEN))) {
                    reveal(&trial, i);
                    changed = true;
                }
            } else if ((trial.cells[i] & CELL_OPEN) && deduce(&trial, i)) {
                changed = true;
            }
        }
        if (!changed && trial.remaining > 0 && trial.hit == MINES_NO_HIT) {
            changed = deduce_subsets(&trial);
        }
    }
    return trial.remaining == 0 && trial.hit == MINES_NO_HIT;
}
