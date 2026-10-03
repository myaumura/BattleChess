#ifndef BC_GAME_H
#define BC_GAME_H
#include "recovered_core.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Original move record: destination, source, special flag, piece, capture. */
typedef struct {
    uint16_t to, from, special;
    uint8_t piece, captured;
} BCMove;
enum {
    BC_GAME_MOVE_CAPACITY = 80,
    BC_GAME_HISTORY_CAPACITY = 103,
    BC_GAME_SEARCH_HISTORY_CAPACITY = BC_GAME_HISTORY_CAPACITY + 24
};
typedef struct {
    Position position;
    /* Original committed slots 1..103 plus hypothetical search slots 104..127.
     * Slot 0 is represented by the lower boundary, not a stored record. */
    BCMove history[BC_GAME_SEARCH_HISTORY_CAPACITY];
    size_t history_count;
} BCGame;
void bc_game_init(BCGame *game);
size_t bc_game_pseudo_moves(const BCGame *game, BCMove out[BC_GAME_MOVE_CAPACITY]);
size_t bc_game_legal_moves(const BCGame *game, BCMove out[BC_GAME_MOVE_CAPACITY]);
int bc_game_apply(BCGame *game, BCMove move, BCGame *undo_snapshot);
void bc_game_undo(BCGame *game, const BCGame *snapshot);
int bc_game_in_check(const BCGame *game);
/* CALCCAST 0xc4dc: bit 0 queenside, bit 1 kingside; identity/history only. */
unsigned bc_game_castling_rights(const BCGame *game, unsigned side);
/* KILLMOVG 0xc8f4: revalidate a previously generated move, not arbitrary input. */
int bc_game_killer_move_valid(const BCGame *game, BCMove move);
/* Trusted SEARCHSM candidate only: apply without regenerating the 80-move list.
 * Position/list state must be consistent. Reject own-king check transactionally.
 * Appends hypothetical history without dropping the retained root records. */
int bc_game_search_apply(BCGame *game, BCMove move);
/* Commit a trusted computer result, retaining only the normal 103-record window. */
int bc_game_commit_search_move(BCGame *game, BCMove move);
/* Exposed for translation checks; not a move validator. */
int bc_game_piece_attacks(const Position *, unsigned piece, unsigned side, unsigned from,
                          unsigned to);
int bc_game_attacks(const Position *position, unsigned side, unsigned square);
#ifdef __cplusplus
}
#endif
#endif
