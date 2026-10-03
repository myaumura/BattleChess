#ifndef BC_SEARCH_H
#define BC_SEARCH_H
#include "game.h"
#include "evaluation.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Original EDEC scratch slots survive separate FINDMOVE calls. Zero once when
 * creating the native computer-player session, not between moves. */
typedef struct {
    BCPawnState pawns[25];
} BCSearchSession;

/* Native host boundary: monotonic milliseconds; poll returns 0 to continue,
 * 1 to force the current best move, or -1 to cancel without playing it. */
typedef struct {
    unsigned max_depth;
    int mate_search;
    int32_t seconds;
    uint64_t (*milliseconds)(void *context);
    int (*poll)(void *context);
    void *context;
    BCSearchSession *session;
} BCSearchLimits;

typedef struct {
    BCMove move;
    BCMove variation[24];
    int16_t score;
    unsigned depth;
    uint64_t candidates;
    int has_move, cancelled, interrupted;
} BCSearchResult;

/* FINDMOVE 0x160b4 and its original selective search. Does not mutate game.
 * Clock/poll callbacks may be NULL for deterministic depth-limited checks. */
int bc_search_find(const BCGame *game,
                    const BCSearchLimits *limits,
                    uint32_t *random_state,
                    BCSearchResult *result);
#ifdef __cplusplus
}
#endif
#endif
