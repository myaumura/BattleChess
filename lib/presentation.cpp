#include "presentation.h"
#include "original.hpp"
/* Original: HANDLESQ, file offset 0x33ea.
 * Purpose: Locate a display square using the original row and column boundaries. */
int original_hit_square(int x, int y, bool flat) {
    const int *row_boundaries = flat ? flat_hit_y : perspective_hit_y;
    const int *column_boundaries = flat ? flat_hit_x : perspective_hit_x;
    int row = 0, column = 0;
    while (row < 9 && y >= row_boundaries[row])
        ++row;
    if (row == 0 || row == 9)
        return -1;
    --row;
    while (column < 9 && x >= column_boundaries[row * 9 + column])
        ++column;
    return column == 0 || column == 9 ? -1 : row * 8 + column - 1;
}
/* Original: SETUPMSQ, file offset 0xfc2e.
 * Purpose: Convert display-square numbering to the engine 0x88 layout. */
int display_to_engine(int square) {
    return (square & 7) | (((square * 2) & 0x70) ^ 0x70);
}
/* Native helper (no direct original address).
 * Purpose: Invert the SETUPMSQ square mapping (file offset 0xfc2e). */
int engine_to_display(int square) {
    return (7 - (square >> 4)) * 8 + (square & 7);
}
/* Original: sub_814a, file offset 0x814a.
 * Purpose: Apply the original opponent-color pixel remap used by CHANGECO (0xa450). */
uint8_t original_pixel(uint8_t value, unsigned side) {
    return side ? opponent_pixel_bytes[value] & 3 : value;
}
