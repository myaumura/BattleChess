#include "recovered_core.h"
#include <string.h>

/* INSERTPI, original file 0x57dc..0x5808. Assembly writes only piece/side
 * at board + square*4, retaining the signed piece-list index at +2.
 * Native safety difference: reject invalid pieces/sides/0x88 squares.
 */
/* Original: INSERTPI, file offset 0x57dc.
 * Purpose: Write piece and side while retaining the piece-list index. */
int insert_piece(Position *position, unsigned piece, unsigned side, unsigned square) {
    if (!position || piece > 6 || side > 1 || square > 0x77 || (square & 0x88))
        return 0;
    position->board[square].piece = (uint8_t)piece;
    position->board[square].side = (uint8_t)side;
    return 1;
}

/* RESETGAM, file 0x5724..0x57be, and clear-board helper 0x4c7a..0x4c8a.
 * The latter clears 0x1e0 bytes at A5-0x6c54 (120 four-byte squares).
 * Back-rank words are from expanded DATA at A5-0x65e0:
 * 3,5,4,2,1,4,5,3 (rook, knight, bishop, queen, king, bishop, knight, rook).
 * Original uses INSERTPI for both back_rank ranks/pawn ranks, then CALCPIEC,
 * then side=0/opponent=1. This is board reset, not the full NEWGAME/UI flow.
 */
/* Original: RESETGAM, file offset 0x5724.
 * Purpose: Restore the initial board and rebuild the original piece-list order. */
void reset_board(Position *position) {
    static const uint8_t back_rank[8] = {3, 5, 4, 2, 1, 4, 5, 3};
    memset(position->board, 0, sizeof position->board);
    for (unsigned file = 0; file < 8; ++file) {
        insert_piece(position, back_rank[file], 0, file);
        insert_piece(position, 6, 0, file + 0x10);
        insert_piece(position, 6, 1, file + 0x60);
        insert_piece(position, back_rank[file], 1, file + 0x70);
    }
    calculate_piece_lists(position);
    position->side = 0;
    position->opponent = 1;
}
