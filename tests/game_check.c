#include "Game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
// Native validation (no original entrypoint): Count legal move-tree leaves to check move
// generation.
static unsigned long perft(const BCGame *g, int depth) {
    if (!depth)
        return 1;
    BCMove moves[80];
    size_t n = bcGameLegalMoves(g, moves);
    unsigned long total = 0;
    for (size_t i = 0; i < n; ++i) {
        BCGame next = *g;
        assert(bcGameApply(&next, moves[i], NULL));
        total += perft(&next, depth - 1);
    }
    return total;
}
// Native validation (no original entrypoint): Apply the requested legal move and return it for
// assertions.
static BCMove play(BCGame *g, int from, int to, int piece) {
    BCMove moves[80];
    size_t n = bcGameLegalMoves(g, moves);
    for (size_t i = 0; i < n; ++i)
        if (moves[i].from == from && moves[i].to == to && moves[i].piece == piece) {
            assert(bcGameApply(g, moves[i], NULL));
            return moves[i];
        }
    assert(!"missing legal move");
    return (BCMove){0};
}
// Native validation (no original entrypoint): Prepare an empty board fixture for focused game
// checks.
static void empty(BCGame *g) {
    memset(g, 0, sizeof *g);
    g->position.opponent = 1;
    insert_piece(&g->position, 1, 0, 4);
    insert_piece(&g->position, 1, 1, 0x74);
}
// Native validation (no original entrypoint): Run the game check regression assertions.
int main(void) {
    BCGame g, original, undo;
    bcGameInit(&g);
    original = g;
    assert(sizeof(BCMove) == 8);
    assert(perft(&g, 1) == 20);
    assert(perft(&g, 2) == 400);
    assert(perft(&g, 3) == 8902);
    BCMove moves[80];
    size_t n = bcGameLegalMoves(&g, moves);
    assert(n == 20);
    assert(!bcGameApply(&g, (BCMove){0x34, 0x14, 0, 5, 0}, NULL));
    assert(memcmp(&g, &original, sizeof g) == 0);
    assert(bcGameApply(&g, moves[0], &undo));
    bcGameUndo(&g, &undo);
    assert(memcmp(&g, &original, sizeof g) == 0);
    play(&g, 0x14, 0x34, 6);
    play(&g, 0x60, 0x50, 6);
    play(&g, 0x34, 0x44, 6);
    play(&g, 0x63, 0x43, 6);
    BCMove ep = play(&g, 0x44, 0x53, 6);
    assert(ep.special == 1);
    assert(!g.position.board[0x43].piece && g.position.board[0x53].piece == 6);
    empty(&g);
    insert_piece(&g.position, 3, 0, 7);
    calculate_piece_lists(&g.position);
    BCMove castle = play(&g, 4, 6, 1);
    assert(castle.special == 1);
    assert(g.position.board[5].piece == 3 && !g.position.board[7].piece);
    empty(&g);
    insert_piece(&g.position, 6, 0, 0x60);
    calculate_piece_lists(&g.position);
    n = bcGameLegalMoves(&g, moves);
    int promotions = 0;
    for (size_t i = 0; i < n; ++i)
        if (moves[i].from == 0x60 && moves[i].to == 0x70)
            ++promotions;
    assert(promotions == 4);
    play(&g, 0x60, 0x70, 5);
    assert(g.position.board[0x70].piece == 5);
    empty(&g);
    insert_piece(&g.position, 3, 1, 0x64);
    calculate_piece_lists(&g.position);
    assert(bcGameInCheck(&g));
    puts("game checks passed: perft 20/400/8902, snapshots, validation, en passant, castling, "
         "promotions, check");
}
