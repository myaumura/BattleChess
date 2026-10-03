#ifndef BC_QUICKDRAW_CONTROL_H
#define BC_QUICKDRAW_CONTROL_H
#include <SDL3/SDL.h>
#include <algorithm>

// Native translation of QuickDraw DrawArc InitOval/BumpOval (1984 source, equal oval axes):
// its unit-aspect squared/odd-number recurrence is floor(sqrt(d*d - ovalY*ovalY)).
inline int quickdraw_round_inset(int diameter, int height, int row) {
    const int edge_row = std::min(row, height - 1 - row);
    if (diameter <= 0 || edge_row >= diameter / 2)
        return 0;
    const int oval_y = 1 - diameter + 2 * edge_row;
    const int square = diameter * diameter - oval_y * oval_y;
    int extent = 0;
    while ((extent + 1) * (extent + 1) <= square)
        ++extent;
    return (diameter - extent) / 2;
}

// Native translation of QuickDraw FrameRoundRect: subtract the pen-inset inner circular roundrect.
inline bool quickdraw_round_frame(SDL_Renderer *renderer, SDL_Rect rect, int diameter, int pen) {
    if (rect.w <= 0 || rect.h <= 0 || pen <= 0 || diameter < 0 || diameter > 256)
        return false;
    diameter = std::min(diameter, std::min(rect.w, rect.h));
    for (int y = 0; y < rect.h; ++y) {
        const int outer = quickdraw_round_inset(diameter, rect.h, y);
        int inner = rect.w / 2 + 1;
        if (y >= pen && y < rect.h - pen && rect.w > pen * 2)
            inner = pen + quickdraw_round_inset(std::max(0, diameter - pen * 2), rect.h - pen * 2,
                                                y - pen);
        for (int x = outer; x < rect.w - outer; ++x)
            if ((x < inner || x >= rect.w - inner) &&
                !SDL_RenderPoint(renderer, float(rect.x + x), float(rect.y + y)))
                return false;
    }
    return true;
}
#endif
