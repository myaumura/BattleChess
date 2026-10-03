#ifndef BC_HINT_OUTLINE_H
#define BC_HINT_OUTLINE_H
#include <cstdint>

/* 
 * Pen zero means redraw without an outline.
 * Time is original TickCount ticks.
 */
struct BCHintPens {
    unsigned from;
    unsigned to;
};

BCHintPens original_hint_pens(uint32_t elapsed_ticks);
uint8_t original_outline_gray(unsigned pen, int logical_x);

#endif
