#include "setup_board.h"
#include <stdlib.h>
#include <string.h>

/* Original: CHECKBOA, file offset 0x6eb8.
 * Purpose: Validate setup positions in original error priority, with native bounds checks. */
const char *bc_setup_validate(const Position *position) {
    unsigned piece_count[2] = {0}, king_count[2] = {0}, king_square[2] = {0};
    if (!position || position->side > 1)
        return "Invalid board data.";
    for (unsigned square = 0; square < 120; ++square) {
        if (square & 0x88)
            continue;
        Square cell = position->board[square];
        if (cell.piece > 6 || cell.side > 1)
            return "Invalid board data.";
        if (!cell.piece)
            continue;
        ++piece_count[cell.side];
        if (cell.piece == 1) {
            ++king_count[cell.side];
            king_square[cell.side] = square;
        }
    }
    if (piece_count[0] > 16)
        return "White has too many pieces.";
    if (piece_count[1] > 16)
        return "Black has too many pieces.";
    if (king_count[0] > 1)
        return "White has too many Kings.";
    if (king_count[1] > 1)
        return "Black has too many Kings.";
    if (!king_count[0])
        return "White needs a King.";
    if (!king_count[1])
        return "Black needs a King.";
    if (abs((int)(king_square[0] / 16) - (int)(king_square[1] / 16)) <= 1 &&
        abs((int)(king_square[0] % 16) - (int)(king_square[1] % 16)) <= 1)
        return "Kings are adjacent.";
    for (unsigned file = 0; file < 8; ++file)
        if (position->board[file].piece == 6 || position->board[file + 0x70].piece == 6)
            return "Pawns in final rank.";
    Position rebuilt = *position;
    calculate_piece_lists(&rebuilt);
    if (bc_game_attacks(&rebuilt, position->side, king_square[position->side ^ 1]))
        return position->side ? "White is in Check." : "Black is in Check.";
    return NULL;
}

/* Original: FUN_000070ce, file offset 0x70ce.
 * Purpose: Commit a valid setup, rebuild piece lists, and clear native move history. */
int bc_setup_commit(BCGame *game, const Position *position) {
    if (!game || bc_setup_validate(position))
        return 0;
    Position rebuilt = *position;
    rebuilt.opponent = rebuilt.side ^ 1;
    calculate_piece_lists(&rebuilt);
    memset(game, 0, sizeof *game);
    game->position = rebuilt;
    return 1;
}
