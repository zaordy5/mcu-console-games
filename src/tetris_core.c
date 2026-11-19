/** @file tetris_core.c
 *  Falling-blocks mechanics with no terminal or platform dependencies.
 */
#include "tetris_core.h"

/** Rotation box side for the pieces that turn around a center cell. */
#define CENTERED_SIDE 3
/** Color index of the straight piece, which rotates in a 4×4 box. */
#define STRAIGHT_COLOR 1u
/** Lines cleared per level. */
#define LINES_PER_LEVEL 5u
/** Milliseconds subtracted from the drop interval per level. */
#define LEVEL_STEP_MS 70u
/** Fastest permitted automatic drop interval. */
#define MIN_INTERVAL_MS 100u
/** Score multiplier for the square of the number of rows cleared together. */
#define LINE_SCORE 100u
/** Square bitmap; turning this piece leaves its cells in place. */
#define SQUARE_SHAPE 0x0066u

/** Initial gravity in milliseconds, indexed by difficulty. */
static const uint16_t initial_ms[] = {
    [TETRIS_EASY] = 850,
    [TETRIS_NORMAL] = 600,
    [TETRIS_HARD] = 350
};

/** Spawn bitmaps of the seven tetrominoes in low-to-high row-major bit order. */
static const uint16_t shapes[TETRIS_SHAPES] = {
    [STRAIGHT_COLOR - 1u] = 0x00f0, /* I: .... / #### */
    0x0071,                       /* J: #... / ###. */
    0x0074,                       /* L: ..#. / ###. */
    SQUARE_SHAPE,                 /* O: .##. / .##. */
    0x0072,                       /* T: .#.. / ###. */
    0x0036,                       /* S: .##. / ##.. */
    0x0063,                       /* Z: ##.. / .##. */
};

/** Test the local cell at (x, y) in a shape bitmap. */
static bool contains(uint16_t shape, int x, int y)
{
    return (shape & (1u << (y * TETRIS_PIECE_SIZE + x))) != 0;
}

/** Test only occupied cells against the board bounds and settled blocks. */
static bool fits(const Tetris *game, uint16_t shape, int left, int top)
{
    for (int y = 0; y < TETRIS_PIECE_SIZE; ++y) {
        for (int x = 0; x < TETRIS_PIECE_SIZE; ++x) {
            if (!contains(shape, x, y)) {
                continue;
            }
            int column = left + x;
            int row = top + y;
            if (column < 0 || column >= TETRIS_WIDTH || row < 0 ||
                row >= TETRIS_HEIGHT || game->board[row][column]) {
                return false;
            }
        }
    }
    return true;
}

/** Generate a pseudorandom value for shuffling the next bag. */
static uint32_t next_random(Tetris *game)
{
    uint32_t value = game->random;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    game->random = value;
    return value;
}

/** Refill and shuffle the seven distinct shape indices. */
static void shuffle(Tetris *game)
{
    for (uint8_t i = 0; i < TETRIS_SHAPES; ++i) {
        game->bag[i] = i;
    }
    for (uint8_t i = TETRIS_SHAPES - 1; i > 0; --i) {
        uint8_t j = (uint8_t)(next_random(game) % (i + 1u));
        uint8_t temporary = game->bag[i];
        game->bag[i] = game->bag[j];
        game->bag[j] = temporary;
    }
    game->next = 0;
}

/** Spawn the next shape at the top center, or mark the game as lost. */
static void spawn(Tetris *game)
{
    uint8_t index = game->bag[game->next++];
    /* Refill now so the preview also works while the last bag piece is active. */
    if (game->next == TETRIS_SHAPES) {
        shuffle(game);
    }
    game->shape = shapes[index];
    game->color = (uint8_t)(index + 1u);
    game->x = (TETRIS_WIDTH - TETRIS_PIECE_SIZE) / 2;
    game->y = 0;
    game->lost = !fits(game, game->shape, game->x, game->y);
}

/** Compact all incomplete rows downward and return the number of full rows. */
static unsigned clear_lines(Tetris *game)
{
    int destination = TETRIS_HEIGHT - 1;
    for (int row = destination; row >= 0; --row) {
        unsigned filled = 0;
        for (int x = 0; x < TETRIS_WIDTH; ++x) {
            filled += game->board[row][x] != 0;
        }
        if (filled == TETRIS_WIDTH) {
            continue;
        }
        if (destination != row) {
            for (int x = 0; x < TETRIS_WIDTH; ++x) {
                game->board[destination][x] = game->board[row][x];
            }
        }
        --destination;
    }
    unsigned cleared = (unsigned)(destination + 1);
    for (; destination >= 0; --destination) {
        for (int x = 0; x < TETRIS_WIDTH; ++x) {
            game->board[destination][x] = 0;
        }
    }
    return cleared;
}

/** Drop a row if possible, otherwise settle the piece, score lines and spawn. */
static void step_down(Tetris *game)
{
    if (fits(game, game->shape, game->x, game->y + 1)) {
        ++game->y;
        return;
    }

    for (int y = 0; y < TETRIS_PIECE_SIZE; ++y) {
        for (int x = 0; x < TETRIS_PIECE_SIZE; ++x) {
            if (contains(game->shape, x, y)) {
                game->board[game->y + y][game->x + x] = game->color;
            }
        }
    }
    unsigned cleared = clear_lines(game);
    game->lines += cleared;
    game->score += LINE_SCORE * cleared * cleared;
    spawn(game);
}

void tetris_reset(Tetris *game, TetrisDifficulty difficulty, uint32_t seed)
{
    for (int y = 0; y < TETRIS_HEIGHT; ++y) {
        for (int x = 0; x < TETRIS_WIDTH; ++x) {
            game->board[y][x] = 0;
        }
    }
    game->difficulty = (unsigned)difficulty < sizeof initial_ms / sizeof initial_ms[0]
        ? difficulty : TETRIS_NORMAL;
    game->score = 0;
    game->lines = 0;
    game->random = seed ? seed : 1u;
    shuffle(game);
    spawn(game);
    game->until_drop = tetris_interval(game);
}

bool tetris_move(Tetris *game, int direction)
{
    if (game->lost || (direction != -1 && direction != 1) ||
        !fits(game, game->shape, game->x + direction, game->y)) {
        return false;
    }
    game->x += direction;
    return true;
}

bool tetris_rotate(Tetris *game)
{
    if (game->lost || game->shape == SQUARE_SHAPE) {
        return false;
    }
    int side = game->color == STRAIGHT_COLOR ? TETRIS_PIECE_SIZE : CENTERED_SIDE;
    uint16_t rotated = 0;
    int min_x = side - 1;
    int max_x = 0;
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            if (contains(game->shape, x, y)) {
                int column = side - 1 - y;
                rotated |= (uint16_t)(1u << (x * TETRIS_PIECE_SIZE + column));
                if (column < min_x) {
                    min_x = column;
                }
                if (column > max_x) {
                    max_x = column;
                }
            }
        }
    }
    int left = game->x;
    if (left + min_x < 0) {
        left = -min_x;
    } else if (left + max_x >= TETRIS_WIDTH) {
        left = TETRIS_WIDTH - 1 - max_x;
    }
    if (rotated == game->shape || !fits(game, rotated, left, game->y)) {
        return false;
    }
    game->x = left;
    game->shape = rotated;
    return true;
}

bool tetris_soft_drop(Tetris *game)
{
    if (game->lost) {
        return false;
    }
    step_down(game);
    game->until_drop = tetris_interval(game);
    return true;
}

bool tetris_tick(Tetris *game, uint32_t elapsed_ms)
{
    if (game->lost) {
        return false;
    }
    bool changed = false;
    while (elapsed_ms >= game->until_drop) {
        elapsed_ms -= game->until_drop;
        step_down(game);
        game->until_drop = tetris_interval(game);
        changed = true;
        if (game->lost) {
            return changed;
        }
    }
    game->until_drop -= elapsed_ms;
    return changed;
}

uint32_t tetris_level(const Tetris *game)
{
    return 1u + game->lines / LINES_PER_LEVEL;
}

uint32_t tetris_interval(const Tetris *game)
{
    uint32_t base = initial_ms[game->difficulty];
    uint32_t steps = tetris_level(game) - 1u;
    uint32_t steps_to_floor = (base - MIN_INTERVAL_MS + LEVEL_STEP_MS - 1u) / LEVEL_STEP_MS;
    if (steps >= steps_to_floor) {
        return MIN_INTERVAL_MS;
    }
    return base - LEVEL_STEP_MS * steps;
}

uint8_t tetris_cell(const Tetris *game, int x, int y)
{
    if (x < 0 || x >= TETRIS_WIDTH || y < 0 || y >= TETRIS_HEIGHT) {
        return 0;
    }

    int local_x = x - game->x;
    int local_y = y - game->y;
    if (!game->lost && local_x >= 0 && local_x < TETRIS_PIECE_SIZE &&
        local_y >= 0 && local_y < TETRIS_PIECE_SIZE &&
        contains(game->shape, local_x, local_y)) {
        return game->color;
    }
    return game->board[y][x];
}

uint8_t tetris_next_color(const Tetris *game)
{
    return game->lost ? 0 : (uint8_t)(game->bag[game->next] + 1u);
}

uint8_t tetris_next_cell(const Tetris *game, int x, int y)
{
    if (game->lost || x < 0 || x >= TETRIS_PIECE_SIZE || y < 0 || y >= TETRIS_PIECE_SIZE) {
        return 0;
    }
    uint8_t index = game->bag[game->next];
    return contains(shapes[index], x, y) ? (uint8_t)(index + 1u) : 0;
}
