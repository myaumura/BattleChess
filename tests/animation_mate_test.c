#include "../lib/animation_plan.h"
#include <assert.h>
#include <stdio.h>
// Native validation (no original entrypoint): Apply a selected legal move while checking that it
// exists.
static void move(BCGame *g, unsigned from, unsigned to) {
    BCMove moves[80];
    size_t n = bcGameLegalMoves(g, moves);
    for (size_t i = 0; i < n; i++)
        if (moves[i].from == from && moves[i].to == to) {
            assert(bcGameApply(g, moves[i], 0));
            return;
        }
    assert(!"missing legal move");
}
// Native validation (no original entrypoint): Run the animation mate test regression assertions.
int main(void) {
    BCGame g;
    bcGameInit(&g);
    BCMove capture;
    assert(!bc_animation_checkmate_move(&g, &capture));
    move(&g, 0x15, 0x25);
    move(&g, 0x64, 0x44);
    move(&g, 0x16, 0x36);
    move(&g, 0x73, 0x37);
    BCMove legal[80];
    assert(bcGameLegalMoves(&g, legal) == 0 && bcGameInCheck(&g));
    assert(bc_animation_checkmate_move(&g, &capture));
    assert(capture.from == 0x37 && capture.to == 4 && capture.piece == 2 && capture.captured == 1 &&
           !capture.special);
    uint8_t display[64];
    setup_display_board(&g.position, display);
    BCAnimationPlan plan;
    assert(bc_animation_plan_build(&plan, display, capture));
    int attacker = 0, defender = 0;
    for (unsigned i = 0; i < plan.count; i++)
        if (plan.nodes[i].type == 5) {
            attacker += plan.nodes[i].dest == 1;
            defender += plan.nodes[i].dest == 0;
        }
    assert(attacker == 1 && defender == 1);
    /* Original list-order path: use a last-moved piece that does not give check. */
    g.history[g.historyCount - 1] = (BCMove){0x44, 0x64, 0, 6, 0};
    assert(bc_animation_checkmate_move(&g, &capture) && capture.from == 0x37);
    puts("original BUILDCHE: Fool's Mate king capture and list fallback passed");
}
