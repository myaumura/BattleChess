#include "hint_outline.h"
#include <cassert>

/* Native check: independent monochrome rows and PRINTHIN timing boundaries. */
int main() {
    static const uint8_t gray[6][2] = {{255, 255}, {255, 255}, {0, 255},
                                       {0, 0},     {0, 255},   {0, 255}};
    for (unsigned pen = 2; pen <= 7; ++pen)
        for (int x = 0; x < 512; ++x)
            assert(original_outline_gray(pen, x) == gray[pen - 2][x & 1]);
    assert(original_outline_gray(0x1c, 0) == 255);
    assert(original_outline_gray(0x1c, 1) == 255);
    for (uint32_t tick = 0; tick < 40; ++tick) {
        BCHintPens pens = original_hint_pens(tick);
        unsigned phase = tick / 2;
        assert(pens.from == 2 + phase % 6);
        if (!phase)
            assert(!pens.to);
        else
            assert(pens.to == original_hint_pens(tick - 2).from);
    }
    assert(original_hint_pens(0).from == 2 && original_hint_pens(0).to == 0);
    assert(original_hint_pens(39).from == 3 && original_hint_pens(39).to == 2);
    assert(!original_hint_pens(40).from && !original_hint_pens(40).to);
    assert(!original_hint_pens(UINT32_MAX).from);
}
