#include "search.h"
#include "evaluation.h"
#include "setup_board.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Native validation (no original entrypoint): request a host cancellation. */
static int cancel(void *context) {
    (void)context;
    return -1;
}
/* Native validation (no original entrypoint): force the best evaluated root move. */
static int force_move(void *context) {
    (void)context;
    return 1;
}
/* Native validation (no original entrypoint): expire after the initial clock read. */
static uint64_t expired_clock(void *context) {
    unsigned *reads = context;
    return (*reads)++ ? 10000 : 0;
}
/* Native validation (no original entrypoint): set a deterministic terminal board. */
static void terminal_board(BCGame *game, unsigned king_square) {
    memset(game, 0, sizeof(*game));
    game->position.side = 1;
    game->position.opponent = 0;
    insert_piece(&game->position, 1, 1, 0x77);
    insert_piece(&game->position, 1, 0, 0x55);
    insert_piece(&game->position, 2, 0, king_square);
    calculate_piece_lists(&game->position);
}
/* Native validation (no original entrypoint): exercise original root RNG count,
 * legal output, preserved state, deterministic iterations and terminal scores. */
int main(void) {
    BCGame game;
    bc_game_init(&game);
    BCGame before = game;
    BCSearchLimits limits = {.max_depth = 1};
    BCSearchResult first, repeated;
    uint32_t seed = 1, expected_seed = 1;
    assert(bc_search_find(&game, &limits, &seed, &first));
    assert(first.has_move && !first.cancelled && first.depth == 1);
    assert(!memcmp(&game, &before, sizeof(game)));
    /* SLBU 0x1730a..0x17342 draws exactly once per legal root candidate. */
    for (unsigned i = 0; i < 20; ++i)
        expected_seed = expected_seed * 0x41c64e6du + 0x3039u;
    assert(seed == expected_seed);
    uint32_t repeat_seed = 1;
    assert(bc_search_find(&game, &limits, &repeat_seed, &repeated));
    assert(!memcmp(&first, &repeated, sizeof(first)) && seed == repeat_seed);
    assert(bc_game_apply(&game, first.move, NULL));
    game = before;
    limits.max_depth = 2;
    seed = 1;
    assert(bc_search_find(&game, &limits, &seed, &first));
    assert(first.has_move && first.depth == 2 && !first.interrupted);
    assert(bc_game_apply(&game, first.move, NULL));
    game = before;
    limits.poll = cancel;
    assert(bc_search_find(&game, &limits, &seed, &first));
    assert(first.cancelled && first.interrupted);
    assert(!memcmp(&game, &before, sizeof(game)));
    limits.poll = force_move;
    assert(bc_search_find(&game, &limits, &seed, &first));
    assert(first.has_move && first.interrupted && !first.cancelled);
    BCSearchSession session = {0};
    session.pawns[24].files[0] = 0x55;
    limits.session = &session;
    assert(bc_search_find(&game, &limits, &seed, &first));
    assert(session.pawns[24].files[0] == 0x55);
    limits.session = NULL;
    limits.poll = NULL;
    unsigned clock_reads = 0;
    limits.milliseconds = expired_clock;
    limits.context = &clock_reads;
    limits.seconds = 3;
    limits.max_depth = 23;
    seed = 1;
    assert(bc_search_find(&game, &limits, &seed, &first));
    assert(first.depth == 1 && first.has_move && seed == expected_seed);
    limits.milliseconds = NULL;
    limits.context = NULL;
    limits.max_depth = 2;
    terminal_board(&game, 0x66);
    assert(bc_search_find(&game, &limits, &seed, &first));
    assert(!first.has_move && first.score == -32000);
    terminal_board(&game, 0x56);
    assert(bc_search_find(&game, &limits, &seed, &first));
    assert(!first.has_move && first.score == 0);
    puts("Original search control checks passed");
}
