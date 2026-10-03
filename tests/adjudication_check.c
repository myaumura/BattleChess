#include "../lib/adjudication.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
// Native validation (no original entrypoint): Apply a selected legal move while checking that it
// exists.
static void move(BCGame *g, unsigned from, unsigned to) {
    BCMove moves[80];
    size_t n = bc_game_legal_moves(g, moves);
    for (size_t i = 0; i < n; ++i)
        if (moves[i].from == from && moves[i].to == to) {
            assert(bc_game_apply(g, moves[i], 0));
            return;
        }
    assert(!"missing legal move");
}
// Native validation (no original entrypoint): Run the adjudication check regression assertions.
int main(void) {
    BCGame g, snapshot;
    bc_game_init(&g);
    assert(bc_adjudicate(&g) == BC_ONGOING && bc_repetitions(&g, 0) == 1);
    for (int i = 0; i < 2; ++i) {
        move(&g, 6, 0x25);
        move(&g, 0x76, 0x55);
        move(&g, 0x25, 6);
        move(&g, 0x55, 0x76);
        assert(bc_repetitions(&g, 0) == i + 2);
        assert(bc_repetitions(&g, 1) == 2);
    }
    assert(bc_fifty_moves(&g) == 8);
    assert(bc_computer_resignation(&g, 8, 0) == 0x282);
    snapshot = g;
    move(&g, 0x14, 0x34);
    assert(bc_fifty_moves(&g) == 0 && bc_repetitions(&g, 0) == 1);
    bc_game_undo(&g, &snapshot);
    assert(bc_repetitions(&g, 0) == 3);
    /* Original special flag is a barrier even for a non-pawn, non-capture. */
    g.history[7].special = 1;
    assert(bc_fifty_moves(&g) == 0 && bc_repetitions(&g, 0) == 1);
    bc_game_init(&g);
    for (int i = 0; i < 100; ++i)
        g.history[i] = (BCMove){(uint16_t)(i + 1), (uint16_t)i, 0, 5, 0};
    g.history_count = 100;
    assert(bc_fifty_moves(&g) == 100 && bc_computer_resignation(&g, 100, 0) == 0x282);
    bc_game_init(&g);
    assert(!bc_computer_resignation(&g, 119, -2175));
    assert(bc_computer_resignation(&g, 120, 0) == 0x282);
    assert(bc_computer_resignation(&g, 0, -2176) == 0x282);
    g.position.side = 1;
    assert(bc_computer_resignation(&g, 120, 0) == 0x2a0);
    bc_game_init(&g);
    move(&g, 0x15, 0x25);
    move(&g, 0x64, 0x44);
    move(&g, 0x16, 0x36);
    move(&g, 0x73, 0x37);
    assert(bc_adjudicate(&g) == BC_CHECKMATE);
    memset(&g, 0, sizeof g);
    g.position.side = 1;
    g.position.opponent = 0;
    insert_piece(&g.position, 1, 1, 0x70);
    insert_piece(&g.position, 1, 0, 0x52);
    insert_piece(&g.position, 2, 0, 0x51);
    calculate_piece_lists(&g.position);
    assert(bc_adjudicate(&g) == BC_STALEMATE);
    assert(!strcmp(bc_ending_message(0x458), "Check and mate."));
    assert(!strcmp(bc_ending_message(0x468), "Stalemate. How boring!"));
    puts("Original repetition chains, reversible-move barriers, resignation gates and endings "
         "passed");
}
