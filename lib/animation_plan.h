#ifndef BC_ANIMATION_PLAN_H
#define BC_ANIMATION_PLAN_H
#include "Game.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t type, status, next, parallel, source, dest, arg, reserved;
} BCAnimationNode;
typedef struct {
    BCAnimationNode nodes[32];
    uint8_t root, last, count;
    uint8_t display[64];
} BCAnimationPlan;
/* Pure original DOWALK graph. move.special must be zero; split special moves first. */
int bc_animation_plan_build(BCAnimationPlan *, const uint8_t display_board[64], BCMove);
/* BUILDCHE 0x739e: post-move checkmated state, presentation only; never apply to engine. */
int bc_animation_checkmate_move(const BCGame *, BCMove *out);
/* Original recursive prelude: returns 1 or 2 sequential ordinary moves.
 * Promotion returns pawn travel; caller then uses original static PUTPIECE replacement. */
size_t bc_animation_split_move(BCMove move, BCMove out[2]);
#ifdef __cplusplus
}
#endif
#endif
