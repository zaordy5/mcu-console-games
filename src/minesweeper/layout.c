/** @file
 *  Parsers for shared layouts and custom mine counts.
 */
#include "layout.h"

/** Read a decimal field and advance to its terminating separator. */
static bool read_decimal(const char **input, uint32_t *value)
{
    const char *digit = *input;
    uint32_t number = 0;
    if (*digit < '0' || *digit > '9') {
        return false;
    }
    do {
        uint32_t next = (uint32_t)(*digit - '0');
        if (number > (UINT32_MAX - next) / 10) {
            return false;
        }
        number = number * 10 + next;
        ++digit;
    } while (*digit >= '0' && *digit <= '9');

    *input = digit;
    *value = number;
    return true;
}

bool mines_parse_count(const char *input, unsigned side, uint8_t *count)
{
    uint32_t number;
    if (!read_decimal(&input, &number) || *input != '\0' || !mines_board_valid(side, number)) {
        return false;
    }
    *count = (uint8_t)number;
    return true;
}

bool mines_parse_code(const char *input, MinesCode *code)
{
    uint32_t fields[5];
    for (unsigned i = 0; i < sizeof fields / sizeof fields[0]; ++i) {
        if (i != 0 && *input++ != '/') {
            return false;
        }
        if (!read_decimal(&input, &fields[i])) {
            return false;
        }
    }
    uint32_t side = fields[0];
    if (*input != '\0' || !mines_board_valid(side, fields[1]) ||
        fields[2] >= side * side || fields[4] > MINES_BANDED) {
        return false;
    }

    code->side = (uint8_t)side;
    code->mine_count = (uint8_t)fields[1];
    code->first = (uint8_t)fields[2];
    code->seed = fields[3];
    code->type = (MinesLayout)fields[4];
    return true;
}
