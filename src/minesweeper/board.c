#include "board.h"

/** Advance a nonzero xorshift32 generator. */
static uint32_t random_next(uint32_t state)
{
    state ^= state << 13;
    state ^= state >> 17;
    return state ^ (state << 5);
}

void mines_board_reset(MinesBoard *board, uint8_t side, uint8_t mine_count)
{
    board->side = side;
    board->mine_count = mine_count;
    board->remaining = side * side - mine_count;
    board->generated = false;
    for (unsigned i = 0; i < side * side; ++i) {
        board->cells[i] = 0;
    }
}

/** Place exactly mine_count distinct mines by partially shuffling candidates. */
static void generate(MinesBoard *board, uint8_t safe, uint32_t seed)
{
    uint8_t candidates[MINES_MAX_CELLS - 1];
    unsigned count = 0;

    for (unsigned i = 0; i < board->side * board->side; ++i) {
        if (i != safe) {
            candidates[count++] = i;
        }
    }
    if (seed == 0) {
        seed = 1;
    }
    for (unsigned i = 0; i < board->mine_count; ++i) {
        seed = random_next(seed);
        unsigned picked = seed % count;
        board->cells[candidates[picked]] |= CELL_MINE;
        candidates[picked] = candidates[--count];
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

uint8_t mines_neighbors(const MinesBoard *board, uint8_t index)
{
    uint8_t neighbors[8];
    unsigned count = adjacent(board, index, neighbors);
    uint8_t mines = 0;

    for (unsigned i = 0; i < count; ++i) {
        if (board->cells[neighbors[i]] & CELL_MINE) {
            ++mines;
        }
    }
    return mines;
}

bool mines_flag(MinesBoard *board, uint8_t index)
{
    if (board->cells[index] & CELL_OPEN) {
        return false;
    }
    board->cells[index] ^= CELL_FLAG;
    return true;
}

/** Reveal a safe cell and queue it once for the iterative flood. */
static void enqueue(MinesBoard *board, uint8_t index, uint8_t *queue, unsigned *tail)
{
    board->cells[index] |= CELL_OPEN;
    --board->remaining;
    queue[(*tail)++] = index;
}

MinesResult mines_open(MinesBoard *board, uint8_t index, uint32_t seed)
{
    uint8_t queue[MINES_MAX_CELLS];
    unsigned tail = 0;

    if (board->cells[index] & (CELL_OPEN | CELL_FLAG)) {
        return MINES_NO_CHANGE;
    }
    if (!board->generated) {
        generate(board, index, seed);
    }
    if (board->cells[index] & CELL_MINE) {
        board->cells[index] |= CELL_OPEN;
        return MINES_LOST;
    }

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
            if (board->cells[neighbor] == 0) {
                enqueue(board, neighbor, queue, &tail);
            }
        }
    }
    return board->remaining == 0 ? MINES_WON : MINES_OPENED;
}
