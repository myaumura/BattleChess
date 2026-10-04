#ifndef BC_SETUP_UI_H
#define BC_SETUP_UI_H

#include "setup_board.h"

struct SetupUI {
    Position originalPosition{};
    Position position{};
    uint8_t heldPiece = 0;
    uint8_t heldSide = 0;

    void begin(const Position &initialPosition);

    /* Original content coordinates; engineSquare=-1 for outside the board. */
    const char *click(int x, int y, int engineSquare);

    /* Caller presents original Clear Board confirmation before invoking clear. */
    void clear();
    void restore();
    const char *done(BCGame &game);

  private:
    void clickBoardSquare(int engineSquare);
    const char *selectPalettePiece(int x, int y);
};

#endif
