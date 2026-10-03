#include "search_moves.h"
#include <assert.h>
#include <string.h>

typedef struct {
    BCMove moves[256];
    size_t count, stop;
} Trace;
/* Native test callback: record SEARCHSM emissions and simulate SEARCHLO cutoff. */
static int record(void *context, BCMove move) {
    Trace *trace = context;
    assert(trace->count < 256);
    trace->moves[trace->count++] = move;
    return trace->stop && trace->count == trace->stop;
}
/* Native fixture: two kings with open board, preserving engine list rebuilding order. */
static BCGame empty_game(void) {
    BCGame game = {0};
    game.position.opponent = 1;
    insert_piece(&game.position, 1, 0, 4);
    insert_piece(&game.position, 1, 1, 0x74);
    calculate_piece_lists(&game.position);
    return game;
}
/* Native check: compare exact original eight-byte record fields. */
static void expect(BCMove move, unsigned to, unsigned from, unsigned special, unsigned piece,
                   unsigned captured) {
    assert(move.to == to && move.from == from && move.special == special && move.piece == piece &&
           move.captured == captured);
}
/* Native check runner: independent expected SEARCHSM order and transactional PERFORM. */
int main(void) {
    BCGame game = empty_game();
    insert_piece(&game.position, 3, 0, 0x33);
    calculate_piece_lists(&game.position);
    Trace trace = {0};
    assert(!bc_search_quiet_from(&game, 0x33, record, &trace));
    static const unsigned rook_destinations[] = {0x23, 0x13, 0x03, 0x43, 0x53, 0x63, 0x73,
                                                 0x32, 0x31, 0x30, 0x34, 0x35, 0x36, 0x37};
    assert(trace.count == sizeof rook_destinations / sizeof *rook_destinations);
    for (size_t i = 0; i < trace.count; ++i)
        expect(trace.moves[i], rook_destinations[i], 0x33, 0, 3, 0);
    trace = (Trace){.stop = 3};
    assert(bc_search_quiet_from(&game, 0x33, record, &trace) && trace.count == 3);

    game = empty_game();
    insert_piece(&game.position, 6, 0, 0x32);
    insert_piece(&game.position, 6, 0, 0x34);
    insert_piece(&game.position, 5, 0, 0x22);
    insert_piece(&game.position, 3, 0, 0x03);
    insert_piece(&game.position, 2, 1, 0x43);
    calculate_piece_lists(&game.position);
    trace = (Trace){0};
    assert(!bc_search_captures_to(&game, 0x43, record, &trace));
    assert(trace.count == 4);
    expect(trace.moves[0], 0x43, 0x32, 0, 6, 2);
    expect(trace.moves[1], 0x43, 0x34, 0, 6, 2);
    expect(trace.moves[2], 0x43, 0x22, 0, 5, 2);
    expect(trace.moves[3], 0x43, 0x03, 0, 3, 2);

    game = empty_game();
    insert_piece(&game.position, 6, 0, 0x63);
    insert_piece(&game.position, 3, 1, 0x72);
    calculate_piece_lists(&game.position);
    trace = (Trace){0};
    assert(!bc_search_captures_to(&game, 0x72, record, &trace) && trace.count == 4);
    for (unsigned i = 0; i < 4; ++i)
        expect(trace.moves[i], 0x72, 0x63, 1, i + 2, 3);
    trace = (Trace){.stop = 2};
    assert(bc_search_quiet_from(&game, 0x63, record, &trace) && trace.count == 2);
    expect(trace.moves[0], 0x73, 0x63, 1, 2, 0);
    expect(trace.moves[1], 0x73, 0x63, 1, 3, 0);

    game = empty_game();
    insert_piece(&game.position, 3, 0, 0);
    insert_piece(&game.position, 3, 0, 7);
    calculate_piece_lists(&game.position);
    assert(bc_game_castling_rights(&game, 0) == 3);
    trace = (Trace){0};
    assert(!bc_search_castles(&game, record, &trace) && trace.count == 2);
    expect(trace.moves[0], 6, 4, 1, 1, 0);
    expect(trace.moves[1], 2, 4, 1, 1, 0);
    game.history[game.history_count++] = (BCMove){7, 0x17, 0, 3, 0};
    assert(bc_game_castling_rights(&game, 0) == 1);
    trace = (Trace){0};
    assert(!bc_search_castles(&game, record, &trace) && trace.count == 1);
    expect(trace.moves[0], 2, 4, 1, 1, 0);

    game = empty_game();
    insert_piece(&game.position, 6, 0, 0x42);
    insert_piece(&game.position, 6, 0, 0x44);
    insert_piece(&game.position, 6, 1, 0x43);
    calculate_piece_lists(&game.position);
    game.history[game.history_count++] = (BCMove){0x43, 0x63, 0, 6, 0};
    trace = (Trace){0};
    assert(!bc_search_en_passant(&game, record, &trace) && trace.count == 2);
    expect(trace.moves[0], 0x53, 0x42, 1, 6, 0);
    expect(trace.moves[1], 0x53, 0x44, 1, 6, 0);
    assert(bc_game_killer_move_valid(&game, trace.moves[0]));
    assert(!bc_game_killer_move_valid(&game, (BCMove){0x52, 0x42, 1, 6, 0}));

    /* CHECK 0xc558 scans root history plus search slots; hypothetical PERFORM
     * must not run STOREMOV and discard an old rook/king destination. */
    game = empty_game();
    insert_piece(&game.position, 3, 0, 7);
    calculate_piece_lists(&game.position);
    game.position.side = 1;
    game.position.opponent = 0;
    game.history_count = BC_GAME_HISTORY_CAPACITY;
    for (size_t i = 0; i < game.history_count; ++i)
        game.history[i] = (BCMove){0x22, 0x10, 0, 5, 0};
    game.history[0] = (BCMove){7, 0x17, 0, 3, 0};
    assert(bc_game_castling_rights(&game, 0) == 0);
    BCGame root_history_game = game;
    assert(bc_game_search_apply(&game, (BCMove){0x73, 0x74, 0, 1, 0}));
    assert(game.history_count == BC_GAME_HISTORY_CAPACITY + 1);
    assert(game.history[0].to == 7 && bc_game_castling_rights(&game, 0) == 0);
    trace = (Trace){0};
    assert(!bc_search_castles(&game, record, &trace) && trace.count == 0);

    for (unsigned ply = 1; ply < 24; ++ply) {
        unsigned side = game.position.side;
        unsigned from = game.position.pieces[side][0].square;
        unsigned to = (from & 7) == 4 ? from - 1 : from + 1;
        assert(bc_game_search_apply(&game, (BCMove){to, from, 0, 1, 0}));
    }
    assert(game.history_count == BC_GAME_SEARCH_HISTORY_CAPACITY);
    assert(game.history[0].to == 7);
    BCGame full_search_game = game;
    assert(!bc_game_search_apply(&game, (BCMove){0x73, 0x74, 0, 1, 0}));
    assert(memcmp(&game, &full_search_game, sizeof game) == 0);
    assert(!bc_game_commit_search_move(&game, (BCMove){0x73, 0x74, 0, 1, 0}));
    assert(!bc_game_apply(&game, (BCMove){0x73, 0x74, 0, 1, 0}, NULL));
    assert(memcmp(&game, &full_search_game, sizeof game) == 0);
    game = root_history_game;
    assert(bc_game_commit_search_move(&game, (BCMove){0x73, 0x74, 0, 1, 0}));
    assert(game.history_count == BC_GAME_HISTORY_CAPACITY && game.history[0].to == 0x22);
    assert(game.history[BC_GAME_HISTORY_CAPACITY - 1].to == 0x73);
    game = root_history_game;
    assert(bc_game_apply(&game, (BCMove){0x73, 0x74, 0, 1, 0}, NULL));
    assert(game.history_count == BC_GAME_HISTORY_CAPACITY && game.history[0].to == 0x22);

    /* SEARCHSM does not inherit INITMOVG's 80-record truncation. */
    game = empty_game();
    static const unsigned queen_squares[] = {0x11, 0x13, 0x15, 0x17, 0x51, 0x53, 0x55, 0x57};
    for (unsigned i = 0; i < 8; ++i)
        insert_piece(&game.position, 2, 0, queen_squares[i]);
    calculate_piece_lists(&game.position);
    trace = (Trace){0};
    for (unsigned i = 0; i < 8; ++i)
        assert(!bc_search_quiet_from(&game, queen_squares[i], record, &trace));
    assert(trace.count > BC_GAME_MOVE_CAPACITY);

    /* Pinned rook may be emitted, but direct search application must roll back. */
    game = empty_game();
    game.position.board[0x74].piece = 0;
    insert_piece(&game.position, 1, 1, 0x77);
    insert_piece(&game.position, 3, 1, 0x74);
    insert_piece(&game.position, 3, 0, 0x14);
    calculate_piece_lists(&game.position);
    BCGame before = game;
    assert(!bc_game_search_apply(&game, (BCMove){0x13, 0x14, 0, 3, 0}));
    assert(memcmp(&before, &game, sizeof game) == 0);
    int white_index = game.position.board[0x14].list_index;
    int black_index = game.position.board[0x74].list_index;
    assert(bc_game_search_apply(&game, (BCMove){0x74, 0x14, 0, 3, 3}));
    assert(game.position.board[0x74].list_index == white_index);
    assert(game.position.pieces[1][black_index].piece == 0);
    assert(game.position.last_piece[1] == before.position.last_piece[1]);
    assert(game.position.side == 1 && game.history_count == 1);

    /* Pawn promotion retains its former pawn slot; do not sort/rebuild lists. */
    game = empty_game();
    insert_piece(&game.position, 6, 0, 0x60);
    insert_piece(&game.position, 6, 0, 0x16);
    calculate_piece_lists(&game.position);
    int pawn_index = game.position.board[0x60].list_index;
    assert(bc_game_search_apply(&game, (BCMove){0x70, 0x60, 1, 2, 0}));
    assert(game.position.board[0x70].list_index == pawn_index);
    assert(game.position.pieces[0][pawn_index].piece == 2);
    assert(game.position.last_nonpawn[0] == pawn_index);
    return 0;
}
