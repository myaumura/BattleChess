#ifndef BC_ADJUDICATION_H
#define BC_ADJUDICATION_H

#include "Game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Original REPEATMO/FIFTYMOV/REPETITI, not FIDE position hashing. */
int bc_repeat_move(BCMove move);
int bc_fifty_moves(const BCGame *game);
int bc_repetitions(const BCGame *game, int search_only);

typedef enum { 
    BC_ONGOING, 
    BC_CHECK, 
    BC_CHECKMATE, 
    BC_STALEMATE 
} BCOutcome;

BCOutcome bc_adjudicate(const BCGame *game);

/* RETURNAN: call only for computer results with equal player modes, after move.
 * total_plies is A5-0x5438, including moves no longer in the history window.
 * Returns original message offset, or zero. */
unsigned bc_computer_resignation(const BCGame *game, int total_plies, int score);
const char *bc_ending_message(unsigned message_offset);

#ifdef __cplusplus
}

#endif
#endif
