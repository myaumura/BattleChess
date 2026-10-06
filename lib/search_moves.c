#include "search_moves.h"
#include <stdlib.h>

/* Original DATA at 0xf99e0/0xf99f0/0xf9a00; SEARCHSM uses descending indices. */
static const int rays[8] = {1, -1, 16, -16, 17, -17, 15, -15};
static const int knights[8] = {14, -14, 18, -18, 31, -31, 33, -33};
static const int pawn_step[2] = {16, -16};

/* Native bounds helper (no original entry point).
 * Purpose: Preserve the original 0x88 rejection before native array access. */
static int valid_square(int square) {
    return square >= 0 && square < 120 && !(square & 0x88);
}

/* Original SEARCHSM, file offset 0x174ea.
 * Purpose: Visit queen/rook/bishop/knight promotions in code order 2..5. */
static int promotions(BCMove move, BCSearchVisit visit, void *context) {
    move.special = 1;
    for (move.piece = 2; move.piece <= 5; ++move.piece)
        if (visit(context, move))
            return 1;
    return 0;
}

/* Original SEARCHSM, file offset 0x17570.
 * Purpose: Capture a specified victim: two pawn origins first, then descending
 * nonpawn list, including promoted entries and skipping captured list holes. */
int bc_search_captures_to(const BCGame *game, unsigned target, BCSearchVisit visit, void *context) {
    if (!game || !visit || !valid_square((int)target))
        return 0;
    const Position *position = &game->position;
    if (position->side > 1)
        return 0;
    BCMove move = {(uint16_t)target, 0, 0, 6, position->board[target].piece};
    int middle = (int)target - pawn_step[position->side];
    for (int from = middle - 1; from <= middle + 1; from += 2) {
        if (!valid_square(from) || position->board[from].piece != 6 ||
            position->board[from].side != position->side)
            continue;
        move.from = (uint16_t)from;
        if (target < 8 || target >= 0x70) {
            if (promotions(move, visit, context))
                return 1;
        } else if (visit(context, move))
            return 1;
    }
    for (int i = position->last_nonpawn[position->side]; i >= 0; --i) {
        PieceEntry entry = position->pieces[position->side][i];
        if (!entry.piece || entry.piece == 6 ||
            !bcGamePieceAttacks(position, entry.piece, position->side, entry.square, target))
            continue;
        move.from = entry.square;
        move.piece = entry.piece;
        if (visit(context, move))
            return 1;
    }
    return 0;
}

/* Original SEARCHSM, file offset 0x17832.
 * Purpose: Visit only empty destinations for one piece, descending direction
 * index and near-to-far rays; pawns visit one step then their initial two-step. */
int bc_search_quiet_from(const BCGame *game, unsigned from, BCSearchVisit visit, void *context) {
    if (!game || !visit || !valid_square((int)from))
        return 0;
    const Position *position = &game->position;
    if (position->side > 1)
        return 0;
    BCMove move = {0, (uint16_t)from, 0, position->board[from].piece, 0};
    if (!move.piece || move.piece > 6)
        return 0;
    if (move.piece == 6) {
        int to = (int)from + pawn_step[position->side];
        /* Native safety: original assumes no pawn already on final rank. */
        if (!valid_square(to) || position->board[to].piece)
            return 0;
        move.to = (uint16_t)to;
        if (to < 8 || to >= 0x70)
            return promotions(move, visit, context);
        if (visit(context, move))
            return 1;
        if (from < 0x18 || from >= 0x60) {
            to += pawn_step[position->side];
            if (valid_square(to) && !position->board[to].piece) {
                move.to = (uint16_t)to;
                if (visit(context, move))
                    return 1;
            }
        }
        return 0;
    }
    int first = move.piece == 4 ? 4 : 0;
    int last = move.piece == 3 ? 3 : 7;
    for (int direction = last; direction >= first; --direction) {
        int step = move.piece == 5 ? knights[direction] : rays[direction];
        for (int to = (int)from + step; valid_square(to) && !position->board[to].piece;
             to += step) {
            move.to = (uint16_t)to;
            if (visit(context, move))
                return 1;
            if (move.piece == 1 || move.piece == 5)
                break;
        }
    }
    return 0;
}

/* Original SEARCHSM, file offset 0x17c96.
 * Purpose: Visit DATA castling records, kingside first, gated by KILLMOVG. */
int bc_search_castles(const BCGame *game, BCSearchVisit visit, void *context) {
    if (!game || !visit || game->position.side > 1)
        return 0;
    unsigned base = game->position.side ? 0x70 : 0;
    for (int wing = 1; wing >= 0; --wing) {
        BCMove move = {(uint16_t)(base + (wing ? 6 : 2)), (uint16_t)(base + 4), 1, 1, 0};
        if (bcGameKillerMoveValid(game, move) && visit(context, move))
            return 1;
    }
    return 0;
}

/* Original SEARCHSM, file offset 0x17da0.
 * Purpose: Following a double pawn move, visit left then right adjacent
 * en-passant candidates. SEARCHSE invokes this even in its tactical phase. */
int bc_search_en_passant(const BCGame *game, BCSearchVisit visit, void *context) {
    if (!game || !visit || !game->historyCount)
        return 0;
    BCMove last = game->history[game->historyCount - 1];
    if (last.piece != 6 || abs((int)last.to - last.from) < 32)
        return 0;
    BCMove move = {(uint16_t)((last.to + last.from) / 2), 0, 1, 6, 0};
    for (int from = (int)last.to - 1; from <= (int)last.to + 1; from += 2) {
        if (!valid_square(from))
            continue;
        move.from = (uint16_t)from;
        if (bcGameKillerMoveValid(game, move) && visit(context, move))
            return 1;
    }
    return 0;
}
