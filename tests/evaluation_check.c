#include "evaluation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Native test helper (no original address).
 * Purpose: Build a sparse, valid root for arithmetic checks independent of move generation. */
static BCGame kings(void) {
    BCGame game = {0};
    insert_piece(&game.position, 1, 0, 0x04);
    insert_piece(&game.position, 1, 1, 0x74);
    game.position.side = 0;
    game.position.opponent = 1;
    calculate_piece_lists(&game.position);
    return game;
}

/* Native recovery checks (no original address).
 * Purpose: Check CALCPVTA/STATEVAL assembly boundaries, including nonstandard mask ordering. */
int main(void) {
    BCGame game;
    bc_game_init(&game);
    BCEvaluation evaluation;
    bc_evaluation_init(&evaluation, &game, 0);
    assert(evaluation.material_total == 19712);
    assert(evaluation.pawn_material == 4096);
    assert(evaluation.material_balance == 0 && evaluation.phase == 45);
    assert(!evaluation.special_endgame && evaluation.root_score == 0);
    assert(evaluation.root_pawns.files[0] == 255 && evaluation.root_pawns.files[1] == 255);
    assert(!evaluation.root_pawns.doubled[0] && !evaluation.root_pawns.doubled[1]);
    /* ED8E/EDAE rank-file values plus F1F8 support bonuses: e2=15, e4=20. */
    assert(evaluation.piece_square[0][5][0x14] == 15);
    assert(evaluation.piece_square[0][5][0x34] == 20);
    BCPawnState next = {0};
    assert(bc_evaluation_delta(&evaluation, &next, &evaluation.root_pawns, 0,
                               (BCMove){0x34, 0x14, 0, 6, 0}, 0) == 5);
    assert(memcmp(&next, &evaluation.root_pawns, sizeof next) == 0);

    BCPawnState masks = {{1, 0}, {0, 0}};
    assert(bc_evaluation_pawns(&masks, 0) == -20);
    masks.doubled[0] = 1;
    assert(bc_evaluation_pawns(&masks, 0) == -68);
    masks.files[0] = 3;
    assert(bc_evaluation_pawns(&masks, 0) == -8);

    game = kings();
    bc_evaluation_init(&evaluation, &game, 0);
    assert(!evaluation.special_endgame && !evaluation.phase && !evaluation.root_score);
    assert(evaluation.piece_square[0][0][0x04] == -6);
    assert(evaluation.piece_square[0][0][0x33] == 0);
    insert_piece(&game.position, 2, 0, 0x33);
    calculate_piece_lists(&game.position);
    bc_evaluation_init(&evaluation, &game, 0);
    /* E7B2 identifies the weaker side; EC1E uses the masked distance, not Manhattan. */
    assert(evaluation.special_endgame && evaluation.material_balance == 2304);
    assert(evaluation.piece_square[0][0][0x04] == 84);
    assert(evaluation.piece_square[1][0][0x74] == 64);
    assert(evaluation.root_score == 2324);

    /* E718 adds each material value as a word: thirty queens wrap 69120 to 3584. */
    game = kings();
    unsigned placed[2] = {0, 0};
    for (unsigned square = 0; square < 120; ++square) {
        if ((square & 0x88) || game.position.board[square].piece)
            continue;
        unsigned side = placed[0] < 15 ? 0 : 1;
        if (placed[side] == 15)
            break;
        insert_piece(&game.position, 2, side, square);
        ++placed[side];
    }
    calculate_piece_lists(&game.position);
    bc_evaluation_init(&evaluation, &game, 0);
    assert(evaluation.material_total == 3584 && evaluation.phase == 0);

    /* Zero positional tables isolate the original STATEVAL material and mask arithmetic. */
    memset(&evaluation, 0, sizeof evaluation);
    evaluation.root_side = 0;
    evaluation.piece_square[0][2][0x33] = 32760;
    assert(bc_evaluation_piece(&evaluation, 3, 0, 0x33) == -31560);
    evaluation.piece_square[0][2][0x33] = 0;
    BCPawnState previous = {0};
    BCMove capture = {0x33, 0x03, 0, 3, 4};
    assert(bc_evaluation_delta(&evaluation, &next, &previous, 0, capture, -255) == 768);
    assert(bc_evaluation_delta(&evaluation, &next, &previous, 0, capture, -256) == 752);
    assert(bc_evaluation_delta(&evaluation, &next, &previous, 0, capture, 256) == 768);
    assert(bc_evaluation_delta(&evaluation, &next, &previous, 1, capture, 256) == 752);
    assert(bc_evaluation_delta(&evaluation, &next, &previous, 0, (BCMove){6, 4, 1, 1, 0}, 0) == 32);
    assert(bc_evaluation_delta(&evaluation, &next, &previous, 0, (BCMove){2, 4, 1, 1, 0}, 0) == 4);

    previous = (BCPawnState){{1, 2}, {0, 0}};
    assert(bc_evaluation_delta(&evaluation, &next, &previous, 0, (BCMove){0x51, 0x40, 1, 6, 0},
                               0) == 236);
    assert(next.files[0] == 2 && next.files[1] == 0);
    /* FA6A reads the stale child mask for promotion, then FB18 overwrites that mask. */
    previous = (BCPawnState){{1, 0}, {0, 0}};
    next = previous;
    assert(bc_evaluation_delta(&evaluation, &next, &previous, 0, (BCMove){0x70, 0x60, 1, 2, 0},
                               0) == 2068);
    assert(next.files[0] == 1);
    next = (BCPawnState){{3, 0}, {0, 0}};
    assert(bc_evaluation_delta(&evaluation, &next, &previous, 0, (BCMove){0x70, 0x60, 1, 2, 0},
                               0) == 2048);
    assert(next.files[0] == 1);
    puts("Original evaluation tables, word arithmetic, special moves and pawn masks passed");
}
