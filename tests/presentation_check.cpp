#include "presentation.h"
#include "original.hpp"
#include <cassert>
#include <cstdio>
#include <iterator>
// Native validation (no original entrypoint): Run the presentation check regression assertions.
int main() {
    assert(std::size(standing_shapes) == 47 && std::size(flat_piece_shapes) == 6);
    for (int flat = 0; flat < 2; ++flat) {
        const int *ys = flat ? flat_hit_y : perspective_hit_y;
        const int *xs = flat ? flat_hit_x : perspective_hit_x;
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col) {
                int x = (xs[row * 9 + col] + xs[row * 9 + col + 1]) / 2;
                int y = (ys[row] + ys[row + 1]) / 2;
                assert(original_hit_square(x, y, flat) == row * 8 + col);
                assert(original_hit_square(xs[row * 9 + col], ys[row], flat) == row * 8 + col);
            }
        assert(original_hit_square(-1, ys[0], flat) == -1);
        assert(original_hit_square(xs[0], ys[0] - 1, flat) == -1);
        assert(original_hit_square(xs[0], ys[8], flat) == -1);
        assert(original_hit_square(xs[8], ys[0], flat) == -1);
    }
    for (int i = 0; i < 64; ++i)
        assert(engine_to_display(display_to_engine(i)) == i);
    for (unsigned byte = 0; byte < 256; ++byte)
        for (int pair = 0; pair < 4; ++pair) {
            auto pixel = uint8_t((byte >> (pair * 2)) & 3);
            assert(original_pixel(pixel, 1) == ((opponent_pixel_bytes[byte] >> (pair * 2)) & 3));
            assert(original_pixel(pixel, 0) == pixel);
        }
    assert(perspective_shapes[6 | (3 << 3)] - 17 == 37); // a-file rook facing inward
    assert(flat_shapes[3] - 17 == 2);                    // knight, not queen
    puts("Original placement, hit-testing, color table and shape checks passed");
}
