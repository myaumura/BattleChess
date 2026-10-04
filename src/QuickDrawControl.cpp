#include "QuickDrawControl.h"
#include <algorithm>

namespace {
    constexpr int kMaxOvalDiameter = 256;

    // Native translation of QuickDraw DrawArc InitOval/BumpOval (1984 source, equal oval axes):
    // its unit-aspect squared/odd-number recurrence is floor(sqrt(d*d - ovalY*ovalY)).
    int roundInset(int diameter, int height, int row) {
        const int edgeRow = std::min(row, height - 1 - row);
        if (diameter <= 0 || edgeRow >= diameter / 2)
            return 0;

        const int ovalY = 1 - diameter + 2 * edgeRow;
        const int square = diameter * diameter - ovalY * ovalY;
        int extent = 0;
        while ((extent + 1) * (extent + 1) <= square)
            ++extent;
        return (diameter - extent) / 2;
    }

    int innerFrameInset(const SDL_Rect &rectangle, int ovalDiameter, int penWidth, int row) {
        if (row < penWidth || row >= rectangle.h - penWidth || rectangle.w <= penWidth * 2)
            // There is no inner area on this row, so keep the entire outer span.
            return rectangle.w / 2 + 1;

        const int innerDiameter = std::max(0, ovalDiameter - penWidth * 2);
        const int innerHeight = rectangle.h - penWidth * 2;
        return penWidth + roundInset(innerDiameter, innerHeight, row - penWidth);
    }
} // namespace

// Native translation of QuickDraw FrameRoundRect: subtract the pen-inset inner circular roundrect.
bool drawQuickDrawRoundFrame(SDL_Renderer *renderer, SDL_Rect rectangle, int ovalDiameter,
                             int penWidth) {
    if (rectangle.w <= 0 || rectangle.h <= 0 || penWidth <= 0 || ovalDiameter < 0 ||
        ovalDiameter > kMaxOvalDiameter)
        return false;

    ovalDiameter = std::min(ovalDiameter, std::min(rectangle.w, rectangle.h));
    for (int y = 0; y < rectangle.h; ++y) {
        const int outerInset = roundInset(ovalDiameter, rectangle.h, y);
        const int innerInset = innerFrameInset(rectangle, ovalDiameter, penWidth, y);
        for (int x = outerInset; x < rectangle.w - outerInset; ++x) {
            const bool isFramePixel = x < innerInset || x >= rectangle.w - innerInset;
            if (isFramePixel &&
                !SDL_RenderPoint(renderer, float(rectangle.x + x), float(rectangle.y + y)))
                return false;
        }
    }
    return true;
}
