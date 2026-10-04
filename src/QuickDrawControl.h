#ifndef BC_QUICKDRAW_CONTROL_H
#define BC_QUICKDRAW_CONTROL_H

#include <SDL3/SDL.h>

// Draw a QuickDraw rounded border using the renderer's current color.
// Return false for invalid geometry or an SDL rendering failure.
bool drawQuickDrawRoundFrame(SDL_Renderer *renderer, SDL_Rect rectangle, int ovalDiameter,
                             int penWidth);

#endif
