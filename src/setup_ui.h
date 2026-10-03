#ifndef BC_SETUP_UI_H
#define BC_SETUP_UI_H

#include "setup_board.h"

struct BCSetupUI {
    Position original, position;
    uint8_t held_piece, held_side;
};

void bc_setup_ui_begin(BCSetupUI *, const Position *);

/* Original content coordinates; engine_square=-1 for outside the board. */
const char *bc_setup_ui_click(BCSetupUI *, int x, int y, int engine_square);

/* Caller presents original Clear Board confirmation before invoking clear. */
void bc_setup_ui_clear(BCSetupUI *);
void bc_setup_ui_restore(BCSetupUI *);
const char *bc_setup_ui_done(BCSetupUI *, BCGame *);

#endif
