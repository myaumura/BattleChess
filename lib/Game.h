#ifndef BC_GAME_H
#define BC_GAME_H

#include "recovered_core.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Original move record: destination, source, special flag, piece, capture. */
typedef struct {
    uint16_t to;      /* Destination in engine 0x88 coordinates. */
    uint16_t from;    /* Source in engine 0x88 coordinates. */
    uint16_t special; /* Castling, en passant, or promotion flag. */
    uint8_t piece;    /* Moving piece, or selected promoted piece. */
    uint8_t captured; /* Captured destination piece; zero when no piece is there. */
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
    /* Number of retained records, including hypothetical moves during search. */
    size_t historyCount;
} BCGame;

/* Game lifecycle and committed moves. */

/* Reset the initial position and clear all retained history. */
void bcGameInit(BCGame *game);

/* Apply an exact legal move record; return 1 on success or 0 on rejection.
 * A non-NULL undoSnapshot distinct from game receives the prior state only on success. */
int bcGameApply(BCGame *game, BCMove move, BCGame *undoSnapshot);

/* Restore the complete caller-owned game snapshot. */
void bcGameUndo(BCGame *game, const BCGame *snapshot);

/* Move generation. Both output buffers must hold BC_GAME_MOVE_CAPACITY records. */

/* Preserve recovered INITMOVG ordering; the 80-move cap precedes king-safety filtering. */
size_t bcGamePseudoMoves(const BCGame *game, BCMove out[BC_GAME_MOVE_CAPACITY]);

/* Filter pseudo moves for own-king safety while retaining their order. */
size_t bcGameLegalMoves(const BCGame *game, BCMove out[BC_GAME_MOVE_CAPACITY]);

/* Board queries. Position board and piece lists must be consistent. */

/* Test the side-to-move king against the opponent's attacks. */
int bcGameInCheck(const BCGame *game);

/* CALCCAST 0xc4dc: bit 0 queenside, bit 1 kingside; identity/history only. */
unsigned bcGameCastlingRights(const BCGame *game, unsigned side);

/* Test whether the given side attacks an engine 0x88 square. */
int bcGameAttacks(const Position *position, unsigned side, unsigned square);

/* Exposed for translation checks; geometry and blockers only, not a move validator. */
int bcGamePieceAttacks(const Position *position, unsigned piece, unsigned side, unsigned from,
                       unsigned to);

/* Trusted search operations. Candidates must come from recovered move generation. */

/* KILLMOVG 0xc8f4: revalidate a previously generated move, not arbitrary input. */
int bcGameKillerMoveValid(const BCGame *game, BCMove move);

/* Trusted SEARCHSM candidate only: apply without regenerating the 80-move list.
 * Position/list state must be consistent. Reject own-king check transactionally.
 * Appends hypothetical history without dropping the retained root records. */
int bcGameSearchApply(BCGame *game, BCMove move);

/* Commit a trusted computer result, retaining only the normal 103-record window. */
int bcGameCommitSearchMove(BCGame *game, BCMove move);

#ifdef __cplusplus
}
#endif

#endif
