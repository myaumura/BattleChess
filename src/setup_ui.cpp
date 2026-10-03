#include "setup_ui.h"

/* BOARDSET 0x2d9c..0x339c. 
 * Host owns menu/palette painting and cursor sprite.
 * The original edits board cells without rebuilding lists until Done. */
// Native platform adapter (no direct original address): keep the original position for Restore and
// start an editable copy. Related BOARDSET begins at file offset 0x2d9c (tools/3rd-party/dump.log).
void bc_setup_ui_begin(BCSetupUI *ui, const Position *position) {
    *ui = BCSetupUI{*position, *position, 0, 0};
}

// Native platform adapter (no direct original address): handle board pickup/drop and palette
// selection from BOARDSET (file offset 0x2d9c); palette reset and sprite-limit checks occur at
// 0x30fc and 0x31bc (tools/3rd-party/dump.log).
const char *bc_setup_ui_click(BCSetupUI *ui, int x, int y, int square) {
    if (square >= 0 && square < 120 && !(square & 0x88)) {
        Square &cell = ui->position.board[square];
        if (ui->held_piece) {
            cell.piece = ui->held_piece;
            cell.side = ui->held_side;
            ui->held_piece = 0;
        } else if (cell.piece) {
            ui->held_piece = cell.piece;
            ui->held_side = cell.side;
            cell.piece = 0;
        }
        return nullptr;
    }
    /* 0x30fc: discard current cursor piece before testing palette. */
    ui->held_piece = 0;
    int row = (y - 20) / 40;
    if (y <= 0 || row < 0 || row > 5)
        return nullptr;
    int side;
    if (x > 15 && x < 45)
        side = 1;
    else if (x > 455 && x < 487)
        side = 0;
    else
        return nullptr;
    unsigned count = 0;
    for (unsigned s = 0; s < 120; ++s)
        if (!(s & 0x88) && ui->position.board[s].piece)
            ++count;
    /* Original 0x31bc scans 32 available sprite slots. */
    if (count >= 32)
        return "Too many pieces.";
    ui->held_piece = (uint8_t)(row + 1);
    ui->held_side = (uint8_t)side;
    return nullptr;
}

// Native platform adapter (no direct original address): clear editable cells after the caller
// confirms the Clear Board action. Related BOARDSET: file offset 0x2d9c (tools/3rd-party/dump.log).
void bc_setup_ui_clear(BCSetupUI *ui) {
    for (unsigned s = 0; s < 120; ++s)
        if (!(s & 0x88))
            ui->position.board[s].piece = 0;
}

// Native platform adapter (no direct original address): restore the saved setup position and
// discard the held piece. Related BOARDSET: file offset 0x2d9c (tools/3rd-party/dump.log).
void bc_setup_ui_restore(BCSetupUI *ui) {
    ui->position = ui->original;
    ui->held_piece = 0;
}

// Native platform adapter (no direct original address): validate the edited board and commit only
// an accepted position. Related BOARDSET: file offset 0x2d9c (tools/3rd-party/dump.log).
const char *bc_setup_ui_done(BCSetupUI *ui, BCGame *game) {
    const char *error = bc_setup_validate(&ui->position);
    ui->held_piece = 0;
    if (!error)
        bc_setup_commit(game, &ui->position);
    return error;
}
