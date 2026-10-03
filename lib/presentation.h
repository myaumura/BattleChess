#ifndef BC_PRESENTATION_H
#define BC_PRESENTATION_H
#include <stdint.h>
/* HANDLESQ 0x33ea: display-row/file hit test against original table boundaries. */
int original_hit_square(int x, int y, bool flat);
int display_to_engine(int square);
int engine_to_display(int square);
/* SHAPES2D 0xa072 -> CHANGECO 0xa450 -> byte remap helper 0x814a. */
uint8_t original_pixel(uint8_t value, unsigned side);
#endif
