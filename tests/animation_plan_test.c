#include "../lib/animation_plan.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Native validation (no original entrypoint): Convert an engine square to its display index for
// graph fixtures.
static unsigned ds(unsigned e) {
    return (e & 7) | (((e >> 1) & 0x38) ^ 0x38);
}
// Native validation (no original entrypoint): Check reachability, bounds and completion of an
// animation graph.
static void graph(const BCAnimationPlan *p) {
    assert(p->count && p->count <= 32);
    unsigned seen = 0, stack[64], n = 1;
    stack[0] = p->root;
    while (n) {
        unsigned i = stack[--n];
        assert(i < p->count);
        assert(!(seen & (1u << i)));
        seen |= 1u << i;
        const BCAnimationNode *a = &p->nodes[i];
        assert(a->source < 64 && a->type >= 1 && a->type <= 8);
        if (a->next != 255)
            stack[n++] = a->next;
        if (a->parallel != 255)
            stack[n++] = a->parallel;
    }
    assert(seen == (p->count == 32 ? ~0u : ((1u << p->count) - 1)));
    assert(p->nodes[p->last].type == 3);
}
// Native validation (no original entrypoint): Run the animation plan test regression assertions.
int main(void) {
    static const unsigned display[7] = {0, 2, 5, 6, 1, 3, 4};
    BCAnimationPlan p;
    uint8_t board[64] = {0};
    unsigned cases = 0;
    for (unsigned piece = 1; piece <= 6; piece++)
        for (unsigned from = 0; from < 128; from++)
            for (unsigned to = 0; to < 128; to++) {
                if ((from & 0x88) || (to & 0x88) || from == to)
                    continue;
                int x = abs((int)(from & 7) - (int)(to & 7)),
                    y = abs((int)(from >> 4) - (int)(to >> 4));
                int valid = piece == 1   ? (x <= 1 && y <= 1)
                            : piece == 2 ? (!x || !y || x == y)
                            : piece == 3 ? (!x || !y)
                            : piece == 4 ? (x == y)
                            : piece == 5 ? ((x == 1 && y == 2) || (x == 2 && y == 1))
                                         : (y == 1 && x <= 1);
                if (!valid)
                    continue;
                for (unsigned captured = 0; captured <= 6; captured++) {
                    memset(board, 0, 64);
                    board[ds(from)] = (uint8_t)(display[piece] | 64);
                    board[ds(to)] = (uint8_t)display[captured];
                    assert(
                        bc_animation_plan_build(&p, board, (BCMove){to, from, 0, piece, captured}));
                    graph(&p);
                    cases++;
                }
            }
    memset(board, 0, 64);
    board[ds(0x14)] = 4 | 64;
    assert(bc_animation_plan_build(&p, board, (BCMove){0x34, 0x14, 0, 6, 0}));
    assert(p.count == 5);
    const unsigned types[5] = {2, 1, 1, 2, 3},
                   destinations[5] = {0, ds(0x24), ds(0x34), 0, ds(0x34)};
    for (unsigned i = 0; i < 5; i++) {
        assert(p.nodes[i].type == types[i] && p.nodes[i].dest == destinations[i]);
        assert(p.nodes[i].source == ds(0x14) && p.nodes[i].parallel == 255);
        assert(p.nodes[i].next == (i == 4 ? 255 : i + 1));
    }
    BCMove moves[2];
    assert(bc_animation_split_move((BCMove){6, 4, 1, 1, 0}, moves) == 2);
    assert(moves[1].from == 7 && moves[1].to == 5 && moves[1].piece == 3);
    assert(bc_animation_split_move((BCMove){0x53, 0x44, 1, 6, 0}, moves) == 2);
    assert(moves[0].to == 0x43 && moves[0].from == 0x44 && moves[0].captured == 6);
    assert(moves[1].from == 0x43 && moves[1].to == 0x53);
    assert(bc_animation_split_move((BCMove){0x70, 0x60, 1, 2, 0}, moves) == 1 &&
           moves[0].piece == 6);
    printf("animation plan graph checks: %u cases\n", cases);
}
