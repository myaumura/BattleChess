#include "hint_outline.h"

/* Original PRINTHIN, file offset 0x66d0; DELAYB 0x7684 / TickCount 0x86b8.
 * Purpose: Twenty source-first phases, two ticks each; the destination retains
 * the previous phase while the source displays the next. Then clear both. */
BCHintPens original_hint_pens(uint32_t elapsed_ticks) {
    unsigned phase = elapsed_ticks / 2;
    if (phase >= 20)
        return {0, 0};
    return {2 + phase % 6, phase ? 2 + (phase - 1) % 6 : 0};
}

/* Original SETAPEN 0xbec4 / DRAWBUFD 0xbe32, monochrome LUT file 0x855c.
 * Purpose: Convert a repeated pen nibble to its addressed two-bit pixel, then
 * map only state 1 to black. States 0,2,3 are opaque white in the screen buffer.
 * Pixel masks at A5 globals 0xfed53 are c0,30,0c,03; phase uses absolute x. */
uint8_t original_outline_gray(unsigned pen, int logical_x) {
    unsigned pixel = (pen >> ((logical_x & 1) ? 0 : 2)) & 3;
    return pixel == 1 ? 0 : 255;
}
