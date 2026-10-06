#include "Game.h"
#include <stdlib.h>
#include <string.h>

/* DATA words at 0xf99e0/0xf99f0/0xf9a00 in recovery_model.json.
 * No Swift engine code is used. This is the board-only INITMOVG/PERFORM path;
 * SEARCH evaluation, opening book, repetition and UI flow are not translated.
 */
static const int raySteps[8] = {1, -1, 16, -16, 17, -17, 15, -15};
static const int knightSteps[8] = {14, -14, 18, -18, 31, -31, 33, -33};
static const int pawnSteps[2] = {16, -16};
/* Native helper (no direct original address).
 * Purpose: Reject indices outside the 0x88 board before native array access. */
static int squareValid(int square) {
    return square >= 0 && square < 120 && !(square & 0x88);
}

/* Native helper (no direct original address).
 * Purpose: Check record bounds before trusted move handling, not validate chess geometry. */
static int moveRecordValid(const BCGame *game, BCMove move) {
    return game && squareValid(move.to) && squareValid(move.from) && move.piece &&
           move.piece <= 6 && move.captured <= 6 && game->position.side <= 1 &&
           game->position.opponent <= 1;
}

/* CALCATTA 0xc11a + PIECEATT 0xc23e. Build the single displacement's
 * table entry from the original DATA masks rather than storing 239 entries.
 */
/* Original: PIECEATT, file offset 0xc23e.
 * Purpose: Test displacement and blockers using CALCATTA (0xc11a) masks. */
static int pieceAttacks(const Position *position, int piece, int side, int from, int to) {
    int displacement = to - from;
    int attackMask = 0;
    int step = 0;
    if (piece == 6)
        return abs(displacement - pawnSteps[side]) == 1;
    for (int direction = 7; direction >= 0; --direction) {
        for (int distance = 1; distance < 8; ++distance) {
            if (displacement == raySteps[direction] * distance) {
                attackMask = 2 + (direction < 4 ? 4 : 8);
                step = raySteps[direction];
                if (distance == 1)
                    attackMask += 1;
            }
        }
        if (displacement == knightSteps[direction]) {
            attackMask = 16;
            step = knightSteps[direction];
        }
    }
    if (piece < 1 || piece > 5 || !(attackMask & (1 << (piece - 1))))
        return 0;
    if (piece == 1 || piece == 5)
        return 1;
    int square = from;
    do {
        square += step;
    } while (square != to && position->board[square].piece == 0);
    return square == to;
}

/* Native helper (no direct original address).
 * Purpose: Validate arguments before the PIECEATT translation (file offset 0xc23e). */
int bcGamePieceAttacks(const Position *position, unsigned piece, unsigned side, unsigned from,
                       unsigned to) {
    if (!position || side > 1 || piece < 1 || piece > 6 || !squareValid((int)from) ||
        !squareValid((int)to))
        return 0;
    return pieceAttacks(position, (int)piece, (int)side, (int)from, (int)to);
}

/* ATTACKSP 0xc348, ATTACKS 0xc410: pawn probes, then descending
 * non-pawn list (including promoted entries), retaining original list order.
 */
/* Original: ATTACKSP, file offset 0xc348.
 * Purpose: Probe pawns then the ATTACKS (0xc410) descending piece list. */
int bcGameAttacks(const Position *position, unsigned side, unsigned square) {
    if (!position || side > 1 || !squareValid((int)square))
        return 0;
    for (int fileOffset = -1; fileOffset <= 1; fileOffset += 2) {
        int sourceSquare = (int)square - pawnSteps[side] + fileOffset;
        if (squareValid(sourceSquare) && position->board[sourceSquare].piece == 6 &&
            position->board[sourceSquare].side == side)
            return 1;
    }
    for (int pieceIndex = position->last_nonpawn[side]; pieceIndex >= 0; --pieceIndex) {
        PieceEntry entry = position->pieces[side][pieceIndex];
        if (entry.piece &&
            pieceAttacks(position, entry.piece, (int)side, entry.square, (int)square))
            return 1;
    }
    return 0;
}

/* PERFORMM 0x4f00 swaps all four bytes, including the saved list index.
 * PERFORMD 0x4f90 deliberately retains side/index for inverse PERFORMI.
 */
/* Original: PERFORMM, file offset 0x4f00.
 * Purpose: Swap complete squares and update the moved piece-list entry. */
static void performMove(Position *position, int to, int from) {
    Square destination = position->board[to];
    position->board[to] = position->board[from];
    position->board[from] = destination;
    Square moved = position->board[to];
    position->pieces[moved.side][moved.list_index].square = (uint8_t)to;
}
/* Original: PERFORMD, file offset 0x4f90.
 * Purpose: Clear a captured piece while retaining side and list index. */
static void performDelete(Position *position, int square) {
    Square *captured = &position->board[square];
    captured->piece = 0;
    position->pieces[captured->side][captured->list_index].piece = 0;
}

/* Native helper extracted from PERFORM (file offset 0x5130).
 * Purpose: Move the rook after PERFORM has moved the castling king. */
static void performCastle(Position *position, BCMove move) {
    int rookTo = (move.to & 7) < 4 ? move.to + 1 : move.to - 1;
    int rookFrom = (move.to & 7) < 4 ? move.to - 2 : move.to + 1;
    performMove(position, rookTo, rookFrom);
}

/* Native helper extracted from PERFORM (file offset 0x5130).
 * Purpose: Replace the pawn in its existing piece-list slot, without rebuilding lists. */
static void performPromotion(Position *position, BCMove move) {
    Square *promoted = &position->board[move.to];
    promoted->piece = move.piece;
    position->pieces[promoted->side][promoted->list_index].piece = move.piece;
    if (position->last_nonpawn[promoted->side] < promoted->list_index)
        position->last_nonpawn[promoted->side] = promoted->list_index;
}

/* Forward PERFORM 0x5130, GENCASTS 0x4eb2, PERFORMC 0x5062.
 * Undo uses a native caller-owned snapshot, not recovered TAKEBACK bookkeeping.
 */
/* Original: PERFORM, file offset 0x5130.
 * Purpose: Apply captures, movement, castling, en passant, and promotion. */
static void perform(Position *position, BCMove move) {
    if (move.captured)
        performDelete(position, move.to);
    performMove(position, move.to, move.from);
    if (!move.special)
        return;
    if (move.piece == 1) {
        performCastle(position, move);
    } else if (move.piece == 6) {
        performDelete(position, (move.from & 0x70) + (move.to & 7));
    } else {
        performPromotion(position, move);
    }
}

/* CHECK 0xc558 tests piece identity and prior move DESTINATIONS, not
 * geometric king check. STOREMOV 0x5634 shifts slots 1..104 to 0..103, then resets slot 0
 * to the zero-piece sentinel: 103 actual prior moves remain.
 */
/* Original: CHECK, file offset 0xc558.
 * Purpose: Check castling piece identity and prior move destinations. */
static int castlePieceUnmoved(const BCGame *game, unsigned side, int square, int piece) {
    const Square occupant = game->position.board[square];
    if (occupant.piece != piece || occupant.side != side)
        return 0;
    for (size_t historyIndex = game->historyCount; historyIndex > 0; --historyIndex)
        if (game->history[historyIndex - 1].to == square)
            return 0;
    return 1;
}
/* Original: CALCCAST, file offset 0xc4dc.
 * Purpose: Report king/rook identity and history rights without path tests. */
unsigned bcGameCastlingRights(const BCGame *game, unsigned side) {
    if (!game || side > 1)
        return 0;
    int homeRank = side ? 0x70 : 0;
    if (!castlePieceUnmoved(game, side, homeRank + 4, 1))
        return 0;
    return (unsigned)castlePieceUnmoved(game, side, homeRank, 3) |
           ((unsigned)castlePieceUnmoved(game, side, homeRank + 7, 3) << 1);
}

/* CALCCAST 0xc4dc / KILLMOVG 0xc8f4. Assembly 0xc996..0xc9c4
 * resolves decompiler parentheses: kingside OR b-file empty, then all three
 * king squares must be unattacked (including the queenside start square).
 */
/* Original: CALCCAST, file offset 0xc4dc.
 * Purpose: Combine castling history with KILLMOVG (0xc8f4) square safety. */
static int castleAllowed(const BCGame *game, int to, int from) {
    const Position *position = &game->position;
    int middle = (to + from) / 2;
    return (bcGameCastlingRights(game, position->side) & (1u << (to > from))) &&
           !position->board[to].piece && !position->board[middle].piece &&
           (to > from || !position->board[to - 1].piece) &&
           !bcGameAttacks(position, position->opponent, from) &&
           !bcGameAttacks(position, position->opponent, to) &&
           !bcGameAttacks(position, position->opponent, middle);
}

/* Original: KILLMOVG, file offset 0xc8f4.
 * Purpose: Revalidate cached/generated records before SEARCHLO. Pawn geometry
 * is deliberately not regenerated: original checks identity, target and, for
 * a two-step record, the midpoint. This is not a public chess-move validator. */
int bcGameKillerMoveValid(const BCGame *game, BCMove move) {
    if (!moveRecordValid(game, move))
        return 0;
    const Position *position = &game->position;
    if ((move.special & 1) && move.piece == 1) {
        if (!squareValid((move.to + move.from) / 2) ||
            (move.to <= move.from && !squareValid((int)move.to - 1)))
            return 0;
        return castleAllowed(game, move.to, move.from);
    }
    if ((move.special & 1) && move.piece == 6) {
        if (!game->historyCount)
            return 0;
        BCMove last = game->history[game->historyCount - 1];
        return last.piece == 6 && abs((int)last.to - last.from) >= 32 &&
               position->board[move.from].piece == 6 &&
               position->board[move.from].side == position->side &&
               (last.to + last.from) / 2 == move.to;
    }
    unsigned piece = move.special ? 6 : move.piece;
    if (position->board[move.from].piece != piece ||
        position->board[move.from].side != position->side ||
        position->board[move.to].piece != move.captured ||
        (move.captured && position->board[move.to].side != position->opponent))
        return 0;
    if (piece == 6) {
        int middle = (move.to + move.from) / 2;
        return abs((int)move.to - move.from) < 32 ||
               (squareValid(middle) && !position->board[middle].piece);
    }
    return bcGamePieceAttacks(position, piece, position->side, move.from, move.to);
}

/* FUN 0xcc42: cap applies to pseudo moves BEFORE king-safety filtering. */
/* Original: FUN_0000cc42, file offset 0xcc42.
 * Purpose: Append a pseudo move without exceeding the original 80-move cap. */
static void appendMove(BCMove moves[BC_GAME_MOVE_CAPACITY], size_t *count, BCMove move) {
    if (*count < BC_GAME_MOVE_CAPACITY)
        moves[(*count)++] = move;
}
/* Original: INITMOVG, file offset 0xcc66.
 * Purpose: Expand final-rank pawn moves into four promotion records. */
static void appendPawnMove(BCMove moves[BC_GAME_MOVE_CAPACITY], size_t *count, BCMove move) {
    if (move.to > 7 && move.to < 0x70)
        appendMove(moves, count, move);
    else { /* INITMOVG 0xcc66 / SEARCHSM 0x174ea */
        move.special = 1;
        for (move.piece = 2; move.piece < 6; ++move.piece)
            appendMove(moves, count, move);
    }
}

/* Native INITMOVG extraction: visit targets in ascending list order, with pawn captures first. */
static void generateCaptures(const Position *position, BCMove moves[BC_GAME_MOVE_CAPACITY],
                             size_t *count) {
    for (int targetIndex = 1; targetIndex <= position->last_piece[position->opponent];
         ++targetIndex) {
        PieceEntry target = position->pieces[position->opponent][targetIndex];
        if (!target.piece)
            continue;
        int pawnOrigin = target.square - pawnSteps[position->side];
        for (int from = pawnOrigin - 1; from <= pawnOrigin + 1; from += 2) {
            if (squareValid(from) && position->board[from].piece == 6 &&
                position->board[from].side == position->side)
                appendPawnMove(moves, count,
                               (BCMove){target.square, (uint16_t)from, 0, 6, target.piece});
        }
        for (int pieceIndex = position->last_nonpawn[position->side]; pieceIndex >= 0;
             --pieceIndex) {
            PieceEntry entry = position->pieces[position->side][pieceIndex];
            if (entry.piece && entry.piece != 6 &&
                pieceAttacks(position, entry.piece, position->side, entry.square, target.square))
                appendMove(moves, count,
                           (BCMove){target.square, entry.square, 0, entry.piece, target.piece});
        }
    }
}

/* Native INITMOVG extraction: kingside precedes queenside. */
static void generateCastles(const BCGame *game, BCMove moves[BC_GAME_MOVE_CAPACITY],
                            size_t *count) {
    int homeRank = game->position.side ? 0x70 : 0;
    for (int wing = 1; wing >= 0; --wing) {
        int to = homeRank + (wing ? 6 : 2);
        int from = homeRank + 4;
        if (castleAllowed(game, to, from))
            appendMove(moves, count, (BCMove){(uint16_t)to, (uint16_t)from, 1, 1, 0});
    }
}

/* Native INITMOVG extraction: emit a single step (or promotions) before a double step. */
static void generateQuietPawnMoves(const Position *position, PieceEntry pawn,
                                   BCMove moves[BC_GAME_MOVE_CAPACITY], size_t *count) {
    int to = pawn.square + pawnSteps[position->side];
    /* Native bounds guard: original assumes no pawns on final rank. */
    if (!squareValid(to) || position->board[to].piece)
        return;
    appendPawnMove(moves, count, (BCMove){(uint16_t)to, pawn.square, 0, 6, 0});
    if (to > 7 && to < 0x70 && (pawn.square > 0x5f || pawn.square < 0x18)) {
        to += pawnSteps[position->side];
        if (squareValid(to) && !position->board[to].piece)
            appendMove(moves, count, (BCMove){(uint16_t)to, pawn.square, 0, 6, 0});
    }
}

/* Native INITMOVG extraction: retain descending direction order and stop at occupied squares. */
static void generateQuietPieceMoves(const Position *position, PieceEntry entry,
                                    BCMove moves[BC_GAME_MOVE_CAPACITY], size_t *count) {
    int firstDirection = entry.piece == 4 ? 4 : 0;
    int lastDirection = entry.piece == 3 ? 3 : 7;
    for (int direction = lastDirection; direction >= firstDirection; --direction) {
        int step = entry.piece == 5 ? knightSteps[direction] : raySteps[direction];
        for (int to = entry.square + step; squareValid(to) && !position->board[to].piece;
             to += step) {
            appendMove(moves, count, (BCMove){(uint16_t)to, entry.square, 0, entry.piece, 0});
            if (entry.piece == 1 || entry.piece == 5)
                break;
        }
    }
}

/* Native INITMOVG extraction: pawns and other pieces share descending piece-list order. */
static void generateQuietMoves(const Position *position, BCMove moves[BC_GAME_MOVE_CAPACITY],
                               size_t *count) {
    for (int pieceIndex = position->last_piece[position->side]; pieceIndex >= 0; --pieceIndex) {
        PieceEntry entry = position->pieces[position->side][pieceIndex];
        if (!entry.piece)
            continue;
        if (entry.piece == 6) {
            generateQuietPawnMoves(position, entry, moves, count);
        } else {
            generateQuietPieceMoves(position, entry, moves, count);
        }
    }
}

/* Native INITMOVG extraction: probe the adjacent files after a double pawn move. */
static void generateEnPassant(const BCGame *game, BCMove moves[BC_GAME_MOVE_CAPACITY],
                              size_t *count) {
    if (!game->historyCount)
        return;
    BCMove last = game->history[game->historyCount - 1];
    if (last.piece != 6 || abs((int)last.to - last.from) < 32)
        return;
    const Position *position = &game->position;
    for (int from = last.to - 1; from <= last.to + 1; from += 2) {
        if (squareValid(from) && position->board[from].piece == 6 &&
            position->board[from].side == position->side)
            appendMove(moves, count,
                       (BCMove){(uint16_t)((last.to + last.from) / 2), (uint16_t)from, 1, 6, 0});
    }
}

/* INITMOVG 0xcc9e, 0xceac, 0xd0ea, same ordering and cap. */
/* Original: INITMOVG, file offset 0xcc9e.
 * Purpose: Generate captures, quiet moves, and special moves in original order; also 0xceac and
 * 0xd0ea. */
size_t bcGamePseudoMoves(const BCGame *game, BCMove moves[BC_GAME_MOVE_CAPACITY]) {
    size_t count = 0;
    generateCaptures(&game->position, moves, &count);
    generateCastles(game, moves, &count);
    generateQuietMoves(&game->position, moves, &count);
    generateEnPassant(game, moves, &count);
    return count;
}

/* Native helper (no direct original address).
 * Purpose: Clear native history and invoke RESETGAM (file offset 0x5724). */
void bcGameInit(BCGame *game) {
    memset(game, 0, sizeof *game);
    reset_board(&game->position);
}
/* Native helper (no direct original address).
 * Purpose: Probe the current king through ATTACKS (file offset 0xc410). */
int bcGameInCheck(const BCGame *game) {
    return bcGameAttacks(&game->position, game->position.opponent,
                         game->position.pieces[game->position.side][0].square);
}
/* Original: ILLEGALM, file offset 0x6312.
 * Purpose: Filter candidates by applying each move and checking its own king. */
size_t bcGameLegalMoves(const BCGame *game, BCMove out[BC_GAME_MOVE_CAPACITY]) {
    BCMove candidates[BC_GAME_MOVE_CAPACITY];
    size_t candidateCount = bcGamePseudoMoves(game, candidates);
    size_t count = 0;
    for (size_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex) {
        Position trial = game->position;
        perform(&trial, candidates[candidateIndex]);
        /* ILLEGALM 0x6312: PERFORM followed by ATTACKS on own king. */
        if (!bcGameAttacks(&trial, trial.opponent, trial.pieces[trial.side][0].square))
            out[count++] = candidates[candidateIndex];
    }
    return count;
}
/* Native helper combining MAKEMOVE 0x6290 and STOREMOV 0x5634.
 * Purpose: Switch sides; only committed moves run the original history-window shift. */
static void finishMove(BCGame *game, BCMove move, int committed) {
    uint8_t movingSide = game->position.side;
    game->position.side = game->position.opponent;
    game->position.opponent = movingSide;
    if (committed && game->historyCount == BC_GAME_HISTORY_CAPACITY) {
        memmove(game->history, game->history + 1, (BC_GAME_HISTORY_CAPACITY - 1) * sizeof move);
        --game->historyCount;
    }
    game->history[game->historyCount++] = move;
}

/* Native SEARCH adapter: PERFORM 0x5130, ILLEGALM 0x6312, MAKEMOVE 0x6290.
 * Purpose: Apply a trusted candidate transactionally; hypothetical history must
 * remain intact so CHECK 0xc558 can scan every root and search destination. */
static int applySearchMove(BCGame *game, BCMove move, int committed) {
    if (!moveRecordValid(game, move))
        return 0;
    if (game->historyCount >
        (committed ? BC_GAME_HISTORY_CAPACITY : BC_GAME_SEARCH_HISTORY_CAPACITY - 1))
        return 0;
    Position previousPosition = game->position;
    perform(&game->position, move);
    if (bcGameInCheck(game)) {
        game->position = previousPosition;
        return 0;
    }
    finishMove(game, move, committed);
    return 1;
}

/* Native helper (no direct original address); related SEARCHLO at 0x168d0.
 * Purpose: Apply one hypothetical move without shifting the root history window. */
int bcGameSearchApply(BCGame *game, BCMove move) {
    return applySearchMove(game, move, 0);
}

/* Native helper (no direct original address); related STOREMOV at 0x5634.
 * Purpose: Commit a trusted computer result with the same history cap as human moves. */
int bcGameCommitSearchMove(BCGame *game, BCMove move) {
    return applySearchMove(game, move, 1);
}

/* Native helper (no direct original address).
 * Purpose: Validate a move, save native undo state, and apply MAKEMOVE (0x6290) side switching. */
int bcGameApply(BCGame *game, BCMove move, BCGame *undoSnapshot) {
    if (!game || game->historyCount > BC_GAME_HISTORY_CAPACITY)
        return 0;
    BCMove legalMoves[BC_GAME_MOVE_CAPACITY];
    size_t candidateCount = bcGameLegalMoves(game, legalMoves);
    for (size_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex) {
        BCMove candidate = legalMoves[candidateIndex];
        if (candidate.to != move.to || candidate.from != move.from ||
            candidate.special != move.special || candidate.piece != move.piece ||
            candidate.captured != move.captured)
            continue;
        if (undoSnapshot && undoSnapshot != game)
            *undoSnapshot = *game;
        perform(&game->position, candidate);
        finishMove(game, candidate, 1);
        return 1;
    }
    return 0;
}
/* Native helper (no direct original address).
 * Purpose: Restore the caller-owned snapshot; replaces original TAKEBACK bookkeeping. */
void bcGameUndo(BCGame *game, const BCGame *snapshot) {
    *game = *snapshot;
}
