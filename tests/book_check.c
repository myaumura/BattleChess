#include "book.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Native validation (no original entrypoint): Run the book check regression assertions.
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    uint8_t book[32000];
    assert(fread(book, 1, sizeof book, f) == sizeof book);
    assert(fgetc(f) == EOF);
    assert(fclose(f) == 0);
    /* Independently inspected root siblings at 1,11855,24317,25262,29316;
     * the next node is the original 0x3f wrap marker. */
    const uint8_t root[] = {1, 3, 17, 7, 5, 1, 3};
    const unsigned thresholds[] = {7, 10, 12, 13, 14, 15, 16};
    for (uint32_t seed = 0; seed < 256; ++seed) {
        uint32_t state = seed;
        uint8_t ordinal = 255;
        uint32_t expected = seed * UINT32_C(0x41c64e6d) + UINT32_C(0x3039);
        unsigned draw = (expected >> 16) & 15, i = 0;
        while (thresholds[i] <= draw)
            ++i;
        assert(book_move(book, sizeof book, NULL, 0, &state, &ordinal));
        assert(ordinal == root[i] && state == expected);
    }
    uint8_t history[] = {1}, ordinal = 255;
    uint32_t state = 1;
    assert(book_move(book, sizeof book, history, 1, &state, &ordinal));
    assert(ordinal == 3);
    history[0] = 63;
    state = 1;
    assert(!book_move(book, sizeof book, history, 1, &state, &ordinal));
    assert(state == 1);
    assert(!book_move(book, sizeof book - 1, NULL, 0, &state, &ordinal));
    memset(book, 0x40, sizeof book);
    book[0] = 0xff;
    state = 0; /* draw 0 selects first node, no traversal */
    history[0] = 1;
    assert(!book_move(book, sizeof book, history, 1, &state, &ordinal));
    puts("Original book traversal, weighting, RNG, and bounds checks passed.");
}
