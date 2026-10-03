#include "setup_ui.h"
#include <cassert>
// Native validation (no original entrypoint): Run the setup ui check regression assertions.
int main() {
    Position p{};
    reset_board(&p);
    BCSetupUI ui;
    bc_setup_ui_begin(&ui, &p);
    assert(bc_setup_ui_click(&ui, 30, 30, -1)); // all 32 sprite slots used
    assert(!bc_setup_ui_click(&ui, 0, 0, 0));
    assert(ui.held_piece == 3 && ui.position.board[0].piece == 0);
    assert(!bc_setup_ui_click(&ui, 0, 0, -1));   // discard rook
    assert(!bc_setup_ui_click(&ui, 30, 70, -1)); // black queen
    assert(ui.held_piece == 2 && ui.held_side == 1);
    assert(!bc_setup_ui_click(&ui, 0, 0, 0x30));
    assert(ui.position.board[0x30].piece == 2 && !ui.held_piece);
    bc_setup_ui_restore(&ui);
    assert(ui.position.board[0].piece == 3 && !ui.position.board[0x30].piece);
    bc_setup_ui_clear(&ui);
    assert(!ui.position.board[0].piece);
    BCGame game{};
    assert(bc_setup_ui_done(&ui, &game));
    bc_setup_ui_restore(&ui);
    assert(!bc_setup_ui_done(&ui, &game));
    assert(game.position.side == p.side && !game.history_count);
}
