#ifndef BC_SEARCH_MOVES_H
#define BC_SEARCH_MOVES_H
#include "game.h"
#ifdef __cplusplus
extern "C" {
#endif
/* SEARCHLO callback: return nonzero to stop. Restore the supplied game's board,
 * piece-list holes/order, side and history before returning to the generator.
 * Unlike INITMOVG, SEARCHSM visits directly and has no 80-move buffer cap. */
typedef int (*BCSearchVisit)(void *context, BCMove move);
int bc_search_captures_to(const BCGame *, unsigned target, BCSearchVisit, void *);
int bc_search_quiet_from(const BCGame *, unsigned from, BCSearchVisit, void *);
int bc_search_castles(const BCGame *, BCSearchVisit, void *);
int bc_search_en_passant(const BCGame *, BCSearchVisit, void *);
#ifdef __cplusplus
}
#endif
#endif
