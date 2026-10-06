#ifndef BC_EVALUATION_H
#define BC_EVALUATION_H
#include "Game.h"
#ifdef __cplusplus
extern "C" {
#endif

/* EDEC: one pair of file masks per side, retained separately at each search ply. */
typedef struct {
    uint16_t files[2], doubled[2];
} BCPawnState;

/* CALCPVTA fixes these tables at the root; do not rebuild them at child nodes. */
typedef struct {
    int16_t piece_square[2][6][120];
    int16_t root_score, material_total, pawn_material, material_balance, phase;
    uint8_t root_side, special_endgame;
    BCPawnState root_pawns;
} BCEvaluation;

/* CALCPVTA 0xe696. Requires a valid board, rebuilt lists and original move history. */
void bc_evaluation_init(BCEvaluation *evaluation, const BCGame *game, unsigned root_side);
/* PIECEPOS 0xf69e: material plus the root-fixed square value. */
int16_t bc_evaluation_piece(const BCEvaluation *evaluation, unsigned piece, unsigned side,
                            unsigned square);
/* PAWNSTRV 0xf5fe: original doubled/isolated-file penalty, not a modern pawn hash. */
int16_t bc_evaluation_pawns(const BCPawnState *pawns, unsigned side);
/* STATEVAL 0xf94c, called BEFORE PERFORM. next and previous must be distinct.
 * Preserve next across siblings: original promotion reads it before the parent copy.
 * capture_reference_score is the current original global AC8C, not a parent score. */
int16_t bc_evaluation_delta(const BCEvaluation *evaluation, BCPawnState *next,
                            const BCPawnState *previous, unsigned moving_side, BCMove move,
                            int16_t capture_reference_score);
#ifdef __cplusplus
}
#endif
#endif
