#include "book.h"

/* Hand translation of NEXTLIBN 0x60dc and PREVIOUS 0x617e. Bounds checks
 * are host input validation; node flags and traversal are original. */
/* Original: PREVIOUS, file offset 0x617e.
 * Purpose: Walk backward through sibling subtrees using original node flags. */
static int rewind_siblings(const uint8_t *book, size_t *cursor) {
    while (*cursor && !(book[*cursor - 1] & 0x40)) {
        int depth = 0;
        do {
            if (!*cursor)
                return 0;
            uint8_t node = book[--*cursor];
            if (node >= 0x80)
                ++depth;
            if (node & 0x40)
                --depth;
        } while (depth);
    }
    return *cursor != 0;
}

/* Original: NEXTLIBN, file offset 0x60dc.
 * Purpose: Advance a book subtree or rewind at a selection terminator. */
static int next(const uint8_t *book, size_t length, size_t *cursor, int selecting) {
    if (*cursor >= length)
        return 0;
    if (book[*cursor] >= 0x80)
        return rewind_siblings(book, cursor);
    int depth = 0;
    do {
        if (*cursor >= length)
            return 0;
        uint8_t node = book[(*cursor)++];
        if (node & 0x40)
            ++depth;
        if (node >= 0x80)
            --depth;
    } while (depth);
    if (*cursor >= length)
        return 0;
    return selecting && book[*cursor] == 0x3f ? rewind_siblings(book, cursor) : 1;
}

/* Original: FINDOPEN, file offset 0x5e46.
 * Purpose: Match supplied move ordinals and select an opening using the original RNG thresholds. */
int book_move(const uint8_t *book, size_t length, const uint8_t *history, size_t plies,
              uint32_t *state, uint8_t *ordinal) {
    if (!book || length != 32000 || book[0] != 0xff || !state || !ordinal || (plies && !history))
        return 0;
    size_t cursor = 1;
    /* FUN_00005fe0: recover ordinals by replaying the original generator;
     * callers supply those ordinals, so the tree matching remains pure. */
    for (size_t i = 0; i < plies; ++i) {
        if (history[i] >= 63 || cursor >= length)
            return 0;
        while ((book[cursor] & 0x3f) != history[i] && book[cursor] < 0x80)
            if (!next(book, length, &cursor, 0))
                return 0;
        if ((book[cursor] & 0x7f) != history[i] + 0x40 || ++cursor >= length)
            return 0;
    }
    /* FINDOPEN 0x5e46; exact A5-0x2584 threshold table and RNG 0x1bdc4. */
    static const uint8_t thresholds[] = {7, 10, 12, 13, 14, 15, 16};
    uint32_t updated = *state * UINT32_C(0x41c64e6d) + UINT32_C(0x3039);
    unsigned draw = (updated >> 16) & 15;
    for (size_t i = 0; thresholds[i] <= draw; ++i)
        if (!next(book, length, &cursor, 1))
            return 0;
    if ((book[cursor] & 0x3f) == 63)
        return 0;
    *ordinal = book[cursor] & 0x3f;
    *state = updated;
    return 1;
}
