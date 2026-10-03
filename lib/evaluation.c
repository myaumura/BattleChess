#include "evaluation.h"
#include <assert.h>
#include <string.h>

/* Original initialized A5 data, big-endian words in recovery_model.json.
 * Material: 0xfed70; centrality: 0xfed7e; pawn rank: 0xfed8e;
 * passed ranks: 0xfed9e; pawn files: 0xfedae; castling: 0xfedbe;
 * attack ranks: 0xfedc2; file masks: 0xfedd2. These are not fitted weights. */
static const int16_t material[7] = {0, 4096, 2304, 1216, 768, 768, 256};
static const int16_t centrality[8] = {3, 2, 1, 0, 0, 1, 2, 3};
static const int16_t pawn_rank[8] = {0, 0, 0, 2, 4, 8, 30, 0};
static const int16_t passed_rank[8] = {0, 0, 10, 20, 40, 60, 70, 0};
static const int16_t pawn_file[8] = {0, 0, 2, 5, 6, 2, 0, 0};
static const int16_t castle_bonus[2] = {4, 32};
static const int16_t attack_rank[8] = {0, 0, 0, 0, 1, 2, 4, 4};
static const uint16_t file_mask[8] = {1, 2, 4, 8, 16, 32, 64, 128};
/* Original initialized direction words at A5 addresses 0xf99e0 and 0xf99f0. */
static const int16_t rays[8] = {1, -1, 16, -16, 17, -17, 15, -15};
static const int16_t knights[8] = {14, -14, 18, -18, 31, -31, 33, -33};

/* Native helper (no direct original address).
 * Purpose: Make 68000 signed-word wrap explicit without signed overflow in host C. */
static int16_t word(int value) {
    unsigned bits = (unsigned)value & 0xffff;
    return (int16_t)(bits < 0x8000 ? (int)bits : (int)bits - 0x10000);
}

/* Native helper (no direct original address).
 * Purpose: Reproduce ASR.W's downward rounding without host signed-shift assumptions. */
static int16_t shift_word(int value, unsigned bits) {
    int signed_value = word(value), divisor = 1 << bits;
    return (int16_t)(signed_value >= 0 ? signed_value / divisor
                                       : -((-signed_value + divisor - 1) / divisor));
}

/* Original: ABS, file offset 0xe614.
 * Purpose: Take a signed-word absolute value, retaining the -32768 wrap case. */
static int16_t absolute_word(int16_t value) {
    return value < 0 ? word(-value) : value;
}

/* Original: PAWNSTRV, file offset 0xf5c2.
 * Purpose: Count set bits in the nonnegative pawn-file masks. */
static int bit_count(uint16_t mask) {
    int count = 0;
    while (mask) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

/* Original: PAWNSTRV, file offset 0xf5fe.
 * Purpose: Penalize doubled and isolated pawn files, including their overlap. */
int16_t bc_evaluation_pawns(const BCPawnState *pawns, unsigned side) {
    uint16_t files = pawns->files[side];
    uint16_t isolated = files & ~((files >> 1) | (files * 2));
    return word(-(bit_count(pawns->doubled[side]) * 8 + bit_count(isolated) * 20 +
                  bit_count(pawns->doubled[side] & isolated) * 40));
}

/* Original: PIECEPOS, file offset 0xf69e.
 * Purpose: Add the original material value to the root-fixed positional entry. */
int16_t bc_evaluation_piece(const BCEvaluation *evaluation, unsigned piece, unsigned side,
                            unsigned square) {
    assert(piece >= 1 && piece <= 6 && side < 2 && square < 120 && !(square & 0x88));
    return word(evaluation->piece_square[side][piece - 1][square] + material[piece]);
}

/* Original: CALCPVTA, file offset 0xe696.
 * Purpose: Build the root-fixed piece-square tables, pawn masks, and static root score. */
void bc_evaluation_init(BCEvaluation *evaluation, const BCGame *game, unsigned root_side) {
    const Position *position = &game->position;
    assert(root_side < 2 && position->side < 2 && position->opponent < 2);
    memset(evaluation, 0, sizeof *evaluation);
    evaluation->root_side = (uint8_t)root_side;
    for (int square = 0; square < 120; ++square) {
        if (square & 0x88)
            continue;
        Square cell = position->board[square];
        if (!cell.piece || cell.piece == 1)
            continue;
        int16_t value = material[cell.piece];
        evaluation->material_total = word(evaluation->material_total + value);
        if (cell.piece == 6)
            evaluation->pawn_material = word(evaluation->pawn_material + material[6]);
        evaluation->material_balance =
            word(evaluation->material_balance + (cell.side ? -value : value));
    }
    int phase_material = word(evaluation->material_total - 0x2000);
    evaluation->phase = (int16_t)((phase_material > 0 ? phase_material : 0) / 0x100);
    unsigned weaker_side = evaluation->material_balance >= 0;
    int16_t imbalance = absolute_word(evaluation->material_balance);
    evaluation->special_endgame =
        word(evaluation->material_total - imbalance) / 2 <= material[4] * 2 &&
        material[3] - material[4] <= imbalance;

    int16_t attacks[2][120] = {{0}};
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            int center = 8 - 3 * (centrality[file] + centrality[rank]);
            if (center < 0)
                center = 0;
            int16_t value =
                word(center + shift_word(attack_rank[rank] * 3 * (evaluation->phase + 8), 5));
            attacks[0][rank * 16 + file] = value;
            attacks[1][(rank * 16 + file) ^ 0x70] = value;
        }
    }
    for (unsigned side = 0; side < 2; ++side) {
        unsigned enemy = side ^ 1;
        int kingside = bc_game_castling_rights(game, enemy) & 2;
        if (evaluation->phase > 0 && !kingside) {
            for (int direction = 0; direction < 8; ++direction) {
                int square = position->pieces[enemy][0].square + rays[direction];
                if (!(square & 0x88))
                    attacks[side][square] =
                        word(attacks[side][square] + shift_word((evaluation->phase + 8) * 12, 5));
            }
        }
    }

    int16_t sliding[2][2][120] = {{{0}}};
    for (int square = 119; square >= 0; --square) {
        if (square & 0x88)
            continue;
        for (unsigned side = 0; side < 2; ++side) {
            for (int direction = 7; direction >= 0; --direction) {
                unsigned piece = direction < 4 ? 3 : 4;
                int16_t sum = 0;
                int unobstructed = 1;
                for (int target = square + rays[direction]; !(target & 0x88);
                     target += rays[direction]) {
                    sum = word(sum + (unobstructed ? attacks[side][target]
                                                   : shift_word(attacks[side][target], 1)));
                    unsigned blocker = position->board[target].piece;
                    if (blocker && blocker != 2 && blocker != piece)
                        unobstructed = 0;
                    if (blocker == 6)
                        break;
                }
                sliding[side][piece - 3][square] =
                    word(sliding[side][piece - 3][square] + shift_word(sum, 2));
            }
        }
    }

    for (int square = 119; square >= 0; --square) {
        if (square & 0x88)
            continue;
        for (unsigned side = 0; side < 2; ++side) {
            int file = square & 7, rank = side ? 7 - (square >> 4) : square >> 4;
            int center = centrality[file] + centrality[rank];
            int enemy_king = position->pieces[side ^ 1][0].square;
            /* 0xec1e..0xec26 really masks the whole sum; it is not Manhattan distance. */
            int king_distance =
                (square - enemy_king + absolute_word((square >> 4) - (enemy_king >> 4))) & 7;
            for (unsigned piece = 1; piece <= 6; ++piece) {
                int16_t value = 0;
                if (evaluation->special_endgame && piece != 6) {
                    if (piece == 1) {
                        if (side == weaker_side) {
                            value = word(128 - 16 * centrality[rank] - 12 * centrality[file]);
                            if (centrality[rank] == 3)
                                value = word(value - 16);
                        } else {
                            value = word(128 - 4 * king_distance);
                            if (centrality[file] == 3 || centrality[rank] > 1)
                                value = word(value - 16);
                        }
                    }
                } else {
                    switch (piece) {
                    case 1:
                        if (evaluation->phase < 1)
                            value = word(-2 * center);
                        break;
                    case 2:
                        value = shift_word(
                            word(sliding[side][0][square] + sliding[side][1][square]), 2);
                        break;
                    case 3:
                    case 4:
                        value = sliding[side][piece - 3][square];
                        break;
                    case 5: {
                        int16_t sum = 0;
                        for (int direction = 0; direction < 8; ++direction) {
                            int target = square + knights[direction];
                            if (!(target & 0x88))
                                sum = word(sum + attacks[side][target]);
                        }
                        value = word(shift_word(sum, 1) - 3 * center);
                        break;
                    }
                    case 6:
                        if (rank != 0 && rank != 7)
                            value = word(pawn_rank[rank] + (rank + 2) * pawn_file[file] - 12);
                        break;
                    }
                }
                evaluation->piece_square[side][piece - 1][square] = value;
            }
        }
    }

    uint16_t pawn_ranks[2][8] = {{0}};
    for (int square = 119; square >= 0; --square) {
        if ((square & 0x88) || position->board[square].piece != 6)
            continue;
        unsigned side = position->board[square].side;
        unsigned rank = side ? 7 - (square >> 4) : square >> 4;
        pawn_ranks[side][rank] |= file_mask[square & 7];
    }
    for (unsigned side = 0; side < 2; ++side) {
        for (int rank = 1; rank < 7; ++rank) {
            evaluation->root_pawns.doubled[side] |=
                pawn_ranks[side][rank] & evaluation->root_pawns.files[side];
            evaluation->root_pawns.files[side] |= pawn_ranks[side][rank];
        }
    }
    evaluation->root_score = word(bc_evaluation_pawns(&evaluation->root_pawns, position->side) -
                                  bc_evaluation_pawns(&evaluation->root_pawns, position->opponent));
    for (unsigned side = 0; side < 2; ++side) {
        unsigned enemy = side ^ 1;
        uint16_t current = 0, left = 0, right = 0, open = 255;
        for (int rank = 1; rank < 7; ++rank) {
            open &= ~(current | left | right);
            uint16_t previous_attacks = left | right;
            current = pawn_ranks[side][rank];
            left = (current * 2) & 255;
            right = (current >> 1) & 255;
            uint16_t next_left = (pawn_ranks[side][rank + 1] * 2) & 255;
            uint16_t next_right = (pawn_ranks[side][rank + 1] >> 1) & 255;
            int square = side ? (rank * 16) ^ 0x70 : rank * 16;
            for (unsigned file = 0; file < 8; ++file, ++square) {
                uint16_t bit = file_mask[file];
                int bonus = (left | right) & bit ? 6 : previous_attacks & bit ? 3 : 0;
                if (next_left & bit)
                    bonus += 3;
                if (next_right & bit)
                    bonus += 3;
                if (current & bit)
                    bonus += 3;
                evaluation->piece_square[side][5][square] =
                    word(evaluation->piece_square[side][5][square] + bonus);
                if ((enemy != root_side || evaluation->phase < 1) && (open & bit))
                    evaluation->piece_square[enemy][5][square] =
                        word(evaluation->piece_square[enemy][5][square] + passed_rank[7 - rank]);
                /* 0xf330 sets ABBC=1, then 0xf3be tests <=0: its rook-bonus body is unreachable. */
            }
        }
    }
    for (int file = 3; file < 5; ++file) {
        if (position->board[file + 0x10].piece == 6 && position->board[file + 0x10].side == 0)
            evaluation->piece_square[0][3][file + 0x20] =
                word(evaluation->piece_square[0][3][file + 0x20] - 20);
        if (position->board[file + 0x60].piece == 6 && position->board[file + 0x60].side == 1)
            evaluation->piece_square[1][3][file + 0x50] =
                word(evaluation->piece_square[1][3][file + 0x50] - 20);
    }
    for (int square = 119; square >= 0; --square) {
        if ((square & 0x88) || !position->board[square].piece)
            continue;
        Square cell = position->board[square];
        int16_t value = bc_evaluation_piece(evaluation, cell.piece, cell.side, square);
        evaluation->root_score =
            word(evaluation->root_score + (cell.side == position->side ? value : -value));
    }
}

/* Original: STATEVAL, file offset 0xf6f2.
 * Purpose: Remove one pawn-file occurrence and return its structural-score delta. */
static int16_t remove_pawn(BCPawnState *next, const BCPawnState *previous, unsigned side,
                           unsigned file) {
    uint16_t keep = (uint16_t)~file_mask[file];
    next->files[side] = next->doubled[side] | (keep & next->files[side]);
    next->doubled[side] &= keep;
    return word(bc_evaluation_pawns(next, side) - bc_evaluation_pawns(previous, side));
}

/* Original: STATEVAL, file offset 0xf7e4.
 * Purpose: Move a pawn between files, retaining the original doubled-file approximation. */
static int16_t move_pawn(BCPawnState *next, const BCPawnState *previous, unsigned side,
                         unsigned destination, unsigned source) {
    uint16_t added = file_mask[destination], removed = file_mask[source];
    next->doubled[side] |= added & next->files[side];
    next->files[side] = added | next->doubled[side] | ((uint16_t)~removed & next->files[side]);
    next->doubled[side] &= (uint16_t)~removed;
    return word(bc_evaluation_pawns(next, side) - bc_evaluation_pawns(previous, side));
}

/* Original: STATEVAL, file offset 0xf94c.
 * Purpose: Evaluate one pre-PERFORM move against root-fixed tables and per-ply pawn masks. */
int16_t bc_evaluation_delta(const BCEvaluation *evaluation, BCPawnState *next,
                            const BCPawnState *previous, unsigned side, BCMove move,
                            int16_t capture_reference_score) {
    assert(next != previous && side < 2);
    unsigned enemy = side ^ 1;
    int16_t delta = 0;
    if (move.special) {
        if (move.piece == 1) {
            unsigned rook_to = (move.to & 7) < 4 ? move.to + 1 : move.to - 1;
            unsigned rook_from = (move.to & 7) < 4 ? move.to - 2 : move.to + 1;
            delta = word(bc_evaluation_piece(evaluation, 3, side, rook_to) -
                         bc_evaluation_piece(evaluation, 3, side, rook_from) +
                         castle_bonus[move.from < move.to]);
        } else if (move.piece == 6) {
            unsigned captured_square = move.to - (side ? -16 : 16);
            delta = bc_evaluation_piece(evaluation, 6, enemy, captured_square);
        } else {
            /* Promotion reads the OLD next slot at 0xfa6a before the copy at 0xfb18.
             * Keep this ordering, even though the following copy discards the mask mutation. */
            delta = word(bc_evaluation_piece(evaluation, move.piece, side, move.from) -
                         bc_evaluation_piece(evaluation, 6, side, move.from) +
                         remove_pawn(next, previous, side, move.from & 7));
        }
    }
    if (move.captured) {
        delta = word(delta + bc_evaluation_piece(evaluation, move.captured, enemy, move.to));
        if (absolute_word(capture_reference_score) >= 256 && move.captured != 6 &&
            ((enemy == evaluation->root_side) == (capture_reference_score >= 0)))
            delta = word(delta - 16);
    }
    *next = *previous;
    if ((move.special | (move.captured != 0)) & (move.piece == 6))
        delta = word(delta + move_pawn(next, previous, side, move.to & 7, move.from & 7));
    if ((move.special & (move.piece == 6)) || move.captured == 6)
        delta = word(delta - remove_pawn(next, previous, enemy, move.to & 7));
    return word(delta + bc_evaluation_piece(evaluation, move.piece, side, move.to) -
                bc_evaluation_piece(evaluation, move.piece, side, move.from));
}
