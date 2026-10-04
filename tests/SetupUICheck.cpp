#include "SetupUI.h"
#include <cassert>

namespace {
    SetupUI makeInitialSetup() {
        Position position{};
        reset_board(&position);
        SetupUI ui{};
        ui.begin(position);
        return ui;
    }

    void checkPieceLimit() {
        auto ui = makeInitialSetup();
        assert(ui.click(30, 30, -1)); // All 32 sprite slots are occupied.
        assert(!ui.heldPiece);
    }

    void checkBoardPickupAndPlacement() {
        auto ui = makeInitialSetup();
        assert(!ui.click(0, 0, 0));
        assert(ui.heldPiece == 3 && ui.position.board[0].piece == 0);
        assert(!ui.click(0, 0, -1));   // Discard the rook.
        assert(!ui.click(30, 70, -1)); // Select a black queen.
        assert(ui.heldPiece == 2 && ui.heldSide == 1);
        assert(!ui.click(0, 0, 0x30));
        assert(ui.position.board[0x30].piece == 2 && !ui.heldPiece);
    }

    void checkRestore() {
        auto ui = makeInitialSetup();
        assert(!ui.click(0, 0, 0));
        assert(!ui.click(0, 0, 0x30));
        assert(!ui.click(0, 0, 1)); // Keep a knight on the cursor during Restore.
        ui.restore();
        assert(ui.position.board[0].piece == 3 && !ui.position.board[0x30].piece);
        assert(!ui.heldPiece);
    }

    void checkClearBoard() {
        auto ui = makeInitialSetup();
        ui.clear();
        for (unsigned square = 0; square < 120; ++square)
            if (!(square & 0x88))
                assert(!ui.position.board[square].piece);
    }

    void checkClearPreservesHeldPiece() {
        auto ui = makeInitialSetup();
        assert(!ui.click(0, 0, 0));
        ui.clear();
        assert(ui.heldPiece == 3);
        assert(!ui.click(0, 0, 0x30));
        assert(ui.position.board[0x30].piece == 3 && !ui.heldPiece);
    }

    void checkDoneRejectsInvalidBoard() {
        auto ui = makeInitialSetup();
        BCGame game{};
        game.position = ui.position;
        game.history_count = 4;
        assert(!ui.click(0, 0, 0));
        ui.clear();
        assert(ui.done(game));
        assert(!ui.heldPiece);
        assert(game.position.board[0].piece == 3 && game.history_count == 4);
    }

    void checkDoneCommitsValidBoard() {
        auto ui = makeInitialSetup();
        const uint8_t initialSide = ui.position.side;
        BCGame game{};
        game.history_count = 4;
        ui.clear();
        ui.restore();
        assert(!ui.done(game));
        assert(game.position.side == initialSide && !game.history_count);
    }
} // namespace

// Native validation (no original entrypoint): setup interaction and commit checks.
int main() {
    checkPieceLimit();
    checkBoardPickupAndPlacement();
    checkRestore();
    checkClearBoard();
    checkClearPreservesHeldPiece();
    checkDoneRejectsInvalidBoard();
    checkDoneCommitsValidBoard();
}
