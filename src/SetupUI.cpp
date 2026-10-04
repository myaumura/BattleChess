#include "SetupUI.h"

/* Native platform adapter for BOARDSET 0x2d9c..0x339c (tools/3rd-party/dump.log).
 * Host owns menu/palette painting and cursor sprite.
 * The original edits board cells without rebuilding lists until Done. */
namespace {
    constexpr unsigned kMaxBoardPieces = 32;
    constexpr int kPaletteTop = 20;
    constexpr int kPaletteRowHeight = 40;
    constexpr int kPaletteRows = 6;

    bool isBoardSquare(int square) {
        return square >= 0 && square < 120 && !(square & 0x88);
    }

    unsigned countBoardPieces(const Position &position) {
        unsigned count = 0;
        for (unsigned square = 0; square < 120; ++square)
            if (isBoardSquare(square) && position.board[square].piece)
                ++count;
        return count;
    }

    int paletteSideAt(int x) {
        if (x > 15 && x < 45)
            return 1;
        if (x > 455 && x < 487)
            return 0;
        return -1;
    }
} // namespace

// Save the original position for Restore and start an editable copy.
void SetupUI::begin(const Position &initialPosition) {
    originalPosition = initialPosition;
    position = initialPosition;
    heldPiece = 0;
    heldSide = 0;
}

const char *SetupUI::click(int x, int y, int engineSquare) {
    if (isBoardSquare(engineSquare)) {
        clickBoardSquare(engineSquare);
        return nullptr;
    }
    return selectPalettePiece(x, y);
}

void SetupUI::clickBoardSquare(int engineSquare) {
    Square &cell = position.board[engineSquare];
    if (heldPiece) {
        cell.piece = heldPiece;
        cell.side = heldSide;
        heldPiece = 0;
    } else if (cell.piece) {
        heldPiece = cell.piece;
        heldSide = cell.side;
        cell.piece = 0;
    }
}

const char *SetupUI::selectPalettePiece(int x, int y) {
    /* 0x30fc: discard current cursor piece before testing palette. */
    heldPiece = 0;
    const int row = (y - kPaletteTop) / kPaletteRowHeight;
    if (y <= 0 || row < 0 || row >= kPaletteRows)
        return nullptr;

    const int side = paletteSideAt(x);
    if (side < 0)
        return nullptr;

    /* Original 0x31bc scans 32 available sprite slots. */
    if (countBoardPieces(position) >= kMaxBoardPieces)
        return "Too many pieces.";
    heldPiece = static_cast<uint8_t>(row + 1);
    heldSide = static_cast<uint8_t>(side);
    return nullptr;
}

// Clear editable cells after the caller confirms the Clear Board action.
void SetupUI::clear() {
    for (unsigned square = 0; square < 120; ++square)
        if (isBoardSquare(square))
            position.board[square].piece = 0;
}

// Restore the saved position and discard the held piece.
void SetupUI::restore() {
    position = originalPosition;
    heldPiece = 0;
}

// Validate the edited board and commit only an accepted position.
const char *SetupUI::done(BCGame &game) {
    const char *error = bc_setup_validate(&position);
    heldPiece = 0;
    if (!error)
        bc_setup_commit(&game, &position);
    return error;
}
