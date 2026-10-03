#ifndef BC_TIME_CONTROL_H
#define BC_TIME_CONTROL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Native names for original A5 globals: level at -0x2592, seconds at -0x528e.
 * Search is not yet connected; this state preserves the original UI settings. */
typedef struct {
    uint8_t level;
    int32_t seconds;
} BCThinkingTime;

/* SETLEVEL 0x6838: select Novice or fixed levels 1..9; custom requires SETTIME. */
int bc_time_select_level(BCThinkingTime *time, unsigned level);
/* SETTIME 0x381e: raise the current budget to one minute before editing. */
int32_t bc_time_dialog_minutes(BCThinkingTime *time);
/* SETTIME 0x38d0 and SETLEVEL 0x68aa: clamp minutes and store seconds. */
void bc_time_set_minutes(BCThinkingTime *time, int32_t minutes);
/* Unnamed setup helper 0x68f0: a search gets at least three seconds. */
int32_t bc_time_search_seconds(const BCThinkingTime *time);

#ifdef __cplusplus
}
#endif
#endif
