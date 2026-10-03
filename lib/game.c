#include "game.h"
#include <stdlib.h>
#include <string.h>

/* DATA words at 0xf99e0/0xf99f0/0xf9a00 in recovery_model.json.
 * No Swift engine code is used. This is the board-only INITMOVG/PERFORM path;
 * SEARCH evaluation, opening book, repetition and UI flow are not translated.
 */
static const int rays[8] = {1, -1, 16, -16, 17, -17, 15, -15};
static const int knights[8] = {14, -14, 18, -18, 31, -31, 33, -33};
static const int pawn_step[2] = {16, -16};
/* Native helper (no direct original address).
 * Purpose: Reject indices outside the 0x88 board before native array access. */
static int square_valid(int s) {
    return s >= 0 && s < 120 && !(s & 0x88);
}

/* CALCATTA 0xc11a + PIECEATT 0xc23e. Build the single displacement's
 * table entry from the original DATA masks rather than storing 239 entries.
 */
/* Original: PIECEATT, file offset 0xc23e.
 * Purpose: Test displacement and blockers using CALCATTA (0xc11a) masks. */
static int piece_attacks(const Position *position, int piece, int side, int from, int to) {
    int delta = to - from, mask = 0, step = 0;
    if (piece == 6)
        return abs(delta - pawn_step[side]) == 1;
    for (int i = 7; i >= 0; --i) {
        for (int distance = 1; distance < 8; ++distance) {
            if (delta == rays[i] * distance) {
                mask = 2 + (i < 4 ? 4 : 8);
                step = rays[i];
                if (distance == 1)
                    mask += 1;
            }
        }
        if (delta == knights[i]) {
            mask = 16;
            step = knights[i];
        }
    }
    if (piece < 1 || piece > 5 || !(mask & (1 << (piece - 1))))
        return 0;
    if (piece == 1 || piece == 5)
        return 1;
    int s = from;
    do {
        s += step;
    } while (s != to && position->board[s].piece == 0);
    return s == to;
}

/* Native helper (no direct original address).
 * Purpose: Validate arguments before the PIECEATT translation (file offset 0xc23e). */
int bc_game_piece_attacks(const Position *position, unsigned piece, unsigned side, unsigned from,
                          unsigned to) {
    if (!position || side > 1 || piece < 1 || piece > 6 || !square_valid((int)from) ||
        !square_valid((int)to))
        return 0;
    return piece_attacks(position, (int)piece, (int)side, (int)from, (int)to);
}

/* ATTACKSP 0xc348, ATTACKS 0xc410: pawn probes, then descending
 * non-pawn list (including promoted entries), retaining original list order.
 */
/* Original: ATTACKSP, file offset 0xc348.
 * Purpose: Probe pawns then the ATTACKS (0xc410) descending piece list. */
int bc_game_attacks(const Position *position, unsigned side, unsigned square) {
    if (!position || side > 1 || !square_valid((int)square))
        return 0;
    for (int d = -1; d <= 1; d += 2) {
        int s = (int)square - pawn_step[side] + d;
        if (square_valid(s) && position->board[s].piece == 6 && position->board[s].side == side)
            return 1;
    }
    for (int i = position->last_nonpawn[side]; i >= 0; --i) {
        PieceEntry entry = position->pieces[side][i];
        if (entry.piece &&
            piece_attacks(position, entry.piece, (int)side, entry.square, (int)square))
            return 1;
    }
    return 0;
}

/* PERFORMM 0x4f00 swaps all four bytes, including the saved list index.
 * PERFORMD 0x4f90 deliberately retains side/index for inverse PERFORMI.
 */
/* Original: PERFORMM, file offset 0x4f00.
 * Purpose: Swap complete squares and update the moved piece-list entry. */
static void perform_move(Position *position, int to, int from) {
    Square tmp = position->board[to];
    position->board[to] = position->board[from];
    position->board[from] = tmp;
    Square moved = position->board[to];
    position->pieces[moved.side][moved.list_index].square = (uint8_t)to;
}
/* Original: PERFORMD, file offset 0x4f90.
 * Purpose: Clear a captured piece while retaining side and list index. */
static void perform_delete(Position *position, int square) {
    Square *s = &position->board[square];
    s->piece = 0;
    position->pieces[s->side][s->list_index].piece = 0;
}
/* Forward PERFORM 0x5130, GENCASTS 0x4eb2, PERFORMC 0x5062.
 * Undo uses a native caller-owned snapshot, not recovered TAKEBACK bookkeeping.
 */
/* Original: PERFORM, file offset 0x5130.
 * Purpose: Apply captures, movement, castling, en passant, and promotion. */
static void perform(Position *position, BCMove move) {
    if (move.captured)
        perform_delete(position, move.to);
    perform_move(position, move.to, move.from);
    if (!move.special)
        return;
    if (move.piece == 1) {
        int rook_to = (move.to & 7) < 4 ? move.to + 1 : move.to - 1;
        int rook_from = (move.to & 7) < 4 ? move.to - 2 : move.to + 1;
        perform_move(position, rook_to, rook_from);
    } else if (move.piece == 6) {
        perform_delete(position, (move.from & 0x70) + (move.to & 7));
    } else {
        Square *s = &position->board[move.to];
        s->piece = move.piece;
        position->pieces[s->side][s->list_index].piece = move.piece;
        if (position->last_nonpawn[s->side] < s->list_index)
            position->last_nonpawn[s->side] = s->list_index;
    }
}

/* CHECK 0xc558 tests piece identity and prior move DESTINATIONS, not
 * geometric king check. STOREMOV 0x5634 shifts slots 1..104 to 0..103, then resets slot 0
 * to the zero-piece sentinel: 103 actual prior moves remain.
 */
/* Original: CHECK, file offset 0xc558.
 * Purpose: Check castling piece identity and prior move destinations. */
static int castle_piece(const BCGame *game, unsigned side, int square, int piece) {
    const Square s = game->position.board[square];
    if (s.piece != piece || s.side != side)
        return 0;
    for (size_t i = game->history_count; i > 0; --i)
        if (game->history[i - 1].to == square)
            return 0;
    return 1;
}
/* Original: CALCCAST, file offset 0xc4dc.
 * Purpose: Report king/rook identity and history rights without path tests. */
unsigned bc_game_castling_rights(const BCGame *game, unsigned side) {
    if (!game || side > 1)
        return 0;
    int base = side ? 0x70 : 0;
    if (!castle_piece(game, side, base + 4, 1))
        return 0;
    return (unsigned)castle_piece(game, side, base, 3) |
           ((unsigned)castle_piece(game, side, base + 7, 3) << 1);
}

/* CALCCAST 0xc4dc / KILLMOVG 0xc8f4. Assembly 0xc996..0xc9c4
 * resolves decompiler parentheses: kingside OR b-file empty, then all three
 * king squares must be unattacked (including the queenside start square).
 */
/* Original: CALCCAST, file offset 0xc4dc.
 * Purpose: Combine castling history with KILLMOVG (0xc8f4) square safety. */
static int castle_allowed(const BCGame *game, int to, int from) {
    const Position *position = &game->position;
    int mid = (to + from) / 2;
    return (bc_game_castling_rights(game, position->side) & (1u << (to > from))) &&
           !position->board[to].piece && !position->board[mid].piece &&
           (to > from || !position->board[to - 1].piece) &&
           !bc_game_attacks(position, position->opponent, from) &&
           !bc_game_attacks(position, position->opponent, to) &&
           !bc_game_attacks(position, position->opponent, mid);
}

/* Original: KILLMOVG, file offset 0xc8f4.
 * Purpose: Revalidate cached/generated records before SEARCHLO. Pawn geometry
 * is deliberately not regenerated: original checks identity, target and, for
 * a two-step record, the midpoint. This is not a public chess-move validator. */
int bc_game_killer_move_valid(const BCGame *game, BCMove move) {
    if (!game || !square_valid(move.to) || !square_valid(move.from) || !move.piece ||
        move.piece > 6 || move.captured > 6 || game->position.side > 1 ||
        game->position.opponent > 1)
        return 0;
    const Position *position = &game->position;
    if ((move.special & 1) && move.piece == 1) {
        if (!square_valid((move.to + move.from) / 2) ||
            (move.to <= move.from && !square_valid((int)move.to - 1)))
            return 0;
        return castle_allowed(game, move.to, move.from);
    }
    if ((move.special & 1) && move.piece == 6) {
        if (!game->history_count)
            return 0;
        BCMove last = game->history[game->history_count - 1];
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
               (square_valid(middle) && !position->board[middle].piece);
    }
    return bc_game_piece_attacks(position, piece, position->side, move.from, move.to);
}

/* FUN 0xcc42: cap applies to pseudo moves BEFORE king-safety filtering. */
/* Original: FUN_0000cc42, file offset 0xcc42.
 * Purpose: Append a pseudo move without exceeding the original 80-move cap. */
static void emit(BCMove moves[80], size_t *count, BCMove move) {
    if (*count < 80)
        moves[(*count)++] = move;
}
/* Original: INITMOVG, file offset 0xcc66.
 * Purpose: Expand final-rank pawn moves into four promotion records. */
static void emit_pawn(BCMove moves[80], size_t *count, BCMove move) {
    if (move.to > 7 && move.to < 0x70)
        emit(moves, count, move);
    else { /* INITMOVG 0xcc66 / SEARCHSM 0x174ea */
        move.special = 1;
        for (move.piece = 2; move.piece < 6; ++move.piece)
            emit(moves, count, move);
    }
}
/* INITMOVG 0xcc9e, 0xceac, 0xd0ea, same ordering and cap. */
/* Original: INITMOVG, file offset 0xcc9e.
 * Purpose: Generate captures, quiet moves, and special moves in original order; also 0xceac and
 * 0xd0ea. */
size_t bc_game_pseudo_moves(const BCGame *game, BCMove moves[80]) {
    const Position *position = &game->position;
    size_t count = 0;
    for (int victim = 1; victim <= position->last_piece[position->opponent]; ++victim) {
        PieceEntry target = position->pieces[position->opponent][victim];
        if (!target.piece)
            continue;
        int origin = target.square - pawn_step[position->side];
        for (int from = origin - 1; from <= origin + 1; from += 2) {
            if (square_valid(from) && position->board[from].piece == 6 &&
                position->board[from].side == position->side)
                emit_pawn(moves, &count,
                          (BCMove){target.square, (uint16_t)from, 0, 6, target.piece});
        }
        for (int i = position->last_nonpawn[position->side]; i >= 0; --i) {
            PieceEntry entry = position->pieces[position->side][i];
            if (entry.piece && entry.piece != 6 &&
                piece_attacks(position, entry.piece, position->side, entry.square, target.square))
                emit(moves, &count,
                     (BCMove){target.square, entry.square, 0, entry.piece, target.piece});
        }
    }
    int base = position->side ? 0x70 : 0;
    for (int wing = 1; wing >= 0; --wing) {
        int to = base + (wing ? 6 : 2), from = base + 4;
        if (castle_allowed(game, to, from))
            emit(moves, &count, (BCMove){(uint16_t)to, (uint16_t)from, 1, 1, 0});
    }
    for (int i = position->last_piece[position->side]; i >= 0; --i) {
        PieceEntry entry = position->pieces[position->side][i];
        if (!entry.piece)
            continue;
        if (entry.piece == 6) {
            int to = entry.square + pawn_step[position->side];
            /* Native bounds guard: original assumes no pawns on final rank. */
            if (!square_valid(to) || position->board[to].piece)
                continue;
            emit_pawn(moves, &count, (BCMove){(uint16_t)to, entry.square, 0, 6, 0});
            if (to > 7 && to < 0x70 && (entry.square > 0x5f || entry.square < 0x18)) {
                to += pawn_step[position->side];
                if (square_valid(to) && !position->board[to].piece)
                    emit(moves, &count, (BCMove){(uint16_t)to, entry.square, 0, 6, 0});
            }
        } else {
            int first = entry.piece == 4 ? 4 : 0, last = entry.piece == 3 ? 3 : 7;
            for (int d = last; d >= first; --d) {
                int step = entry.piece == 5 ? knights[d] : rays[d];
                for (int to = entry.square + step; square_valid(to) && !position->board[to].piece;
                     to += step) {
                    emit(moves, &count, (BCMove){(uint16_t)to, entry.square, 0, entry.piece, 0});
                    if (entry.piece == 1 || entry.piece == 5)
                        break;
                }
            }
        }
    }
    if (game->history_count) {
        BCMove last = game->history[game->history_count - 1];
        if (last.piece == 6 && abs((int)last.to - last.from) >= 32) {
            for (int from = last.to - 1; from <= last.to + 1; from += 2) {
                if (square_valid(from) && position->board[from].piece == 6 &&
                    position->board[from].side == position->side)
                    emit(moves, &count,
                         (BCMove){(uint16_t)((last.to + last.from) / 2), (uint16_t)from, 1, 6, 0});
            }
        }
    }
    return count;
}

/* Native helper (no direct original address).
 * Purpose: Clear native history and invoke RESETGAM (file offset 0x5724). */
void bc_game_init(BCGame *game) {
    memset(game, 0, sizeof *game);
    reset_board(&game->position);
}
/* Native helper (no direct original address).
 * Purpose: Probe the current king through ATTACKS (file offset 0xc410). */
int bc_game_in_check(const BCGame *game) {
    return bc_game_attacks(&game->position, game->position.opponent,
                           game->position.pieces[game->position.side][0].square);
}
/* Original: ILLEGALM, file offset 0x6312.
 * Purpose: Filter candidates by applying each move and checking its own king. */
size_t bc_game_legal_moves(const BCGame *game, BCMove out[80]) {
    BCMove candidates[80];
    size_t candidate_count = bc_game_pseudo_moves(game, candidates), count = 0;
    for (size_t i = 0; i < candidate_count; ++i) {
        Position trial = game->position;
        perform(&trial, candidates[i]);
        /* ILLEGALM 0x6312: PERFORM followed by ATTACKS on own king. */
        if (!bc_game_attacks(&trial, trial.opponent, trial.pieces[trial.side][0].square))
            out[count++] = candidates[i];
    }
    return count;
}
/* Native helper combining MAKEMOVE 0x6290 and STOREMOV 0x5634.
 * Purpose: Switch sides; only committed moves run the original history-window shift. */
static void finish_move(BCGame *game, BCMove move, int committed) {
    uint8_t side = game->position.side;
    game->position.side = game->position.opponent;
    game->position.opponent = side;
    if (committed && game->history_count == BC_GAME_HISTORY_CAPACITY) {
        memmove(game->history, game->history + 1, (BC_GAME_HISTORY_CAPACITY - 1) * sizeof move);
        --game->history_count;
    }
    game->history[game->history_count++] = move;
}

/* Native SEARCH adapter: PERFORM 0x5130, ILLEGALM 0x6312, MAKEMOVE 0x6290.
 * Purpose: Apply a trusted candidate transactionally; hypothetical history must
 * remain intact so CHECK 0xc558 can scan every root and search destination. */
static int apply_search_move(BCGame *game, BCMove move, int committed) {
    if (!game || !square_valid(move.to) || !square_valid(move.from) || !move.piece ||
        move.piece > 6 || move.captured > 6 || game->position.side > 1 ||
        game->position.opponent > 1)
        return 0;
    if (game->history_count >
        (committed ? BC_GAME_HISTORY_CAPACITY : BC_GAME_SEARCH_HISTORY_CAPACITY - 1))
        return 0;
    Position before = game->position;
    perform(&game->position, move);
    if (bc_game_in_check(game)) {
        game->position = before;
        return 0;
    }
    finish_move(game, move, committed);
    return 1;
}

/* Native helper (no direct original address); related SEARCHLO at 0x168d0.
 * Purpose: Apply one hypothetical move without shifting the root history window. */
int bc_game_search_apply(BCGame *game, BCMove move) {
    return apply_search_move(game, move, 0);
}

/* Native helper (no direct original address); related STOREMOV at 0x5634.
 * Purpose: Commit a trusted computer result with the same history cap as human moves. */
int bc_game_commit_search_move(BCGame *game, BCMove move) {
    return apply_search_move(game, move, 1);
}

/* Native helper (no direct original address).
 * Purpose: Validate a move, save native undo state, and apply MAKEMOVE (0x6290) side switching. */
int bc_game_apply(BCGame *game, BCMove move, BCGame *undo) {
    if (!game || game->history_count > BC_GAME_HISTORY_CAPACITY)
        return 0;
    BCMove moves[80];
    size_t candidate_count = bc_game_legal_moves(game, moves);
    for (size_t i = 0; i < candidate_count; ++i) {
        BCMove candidate = moves[i];
        if (candidate.to != move.to || candidate.from != move.from ||
            candidate.special != move.special || candidate.piece != move.piece ||
            candidate.captured != move.captured)
            continue;
        if (undo && undo != game)
            *undo = *game;
        perform(&game->position, candidate);
        finish_move(game, candidate, 1);
        return 1;
    }
    return 0;
}
/* Native helper (no direct original address).
 * Purpose: Restore the caller-owned snapshot; replaces original TAKEBACK bookkeeping. */
void bc_game_undo(BCGame *game, const BCGame *snapshot) {
    *game = *snapshot;
}
