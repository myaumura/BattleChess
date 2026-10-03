#ifndef BC_SETUP_BOARD_H
#define BC_SETUP_BOARD_H
#include "game.h"
#ifdef __cplusplus
extern "C" {
#endif
/* CHECKBOA 0x6eb8: NULL means valid; original error priority/text. */
const char *bc_setup_validate(const Position *position);
int bc_setup_commit(BCGame *game, const Position *position);
#ifdef __cplusplus
}
#endif
#endif
