#include "time_control.h"

/* Original: SETLEVEL, file offset 0x6838 (budget branches 0x687e..0x68da).
 * Preserve the original non-linear level scale instead of inventing difficulty.
 * Native validation rejects indices outside MENU 404's fixed-level entries. */
int bc_time_select_level(BCThinkingTime *time, unsigned level) {
    if (level > 9)
        return 0;
    time->level = (uint8_t)level;
    time->seconds = level == 0 ? 6 : 5 << (level - 1);
    return 1;
}

/* Original: SETTIME, file offset 0x381e..0x3844. Show whole minutes, first
 * raising sub-minute budgets to 60 seconds exactly as the original dialog does. */
int32_t bc_time_dialog_minutes(BCThinkingTime *time) {
    if (time->seconds < 60)
        time->seconds = 60;
    return time->seconds / 60;
}

/* Original: SETTIME clamp at 0x38d0..0x38f2, SETLEVEL multiply at 0x68aa.
 * Keep valid custom budgets bounded so the original signed long cannot overflow. */
void bc_time_set_minutes(BCThinkingTime *time, int32_t minutes) {
    if (minutes > 10000)
        minutes = 10000;
    if (minutes < 1)
        minutes = 1;
    time->level = 10;
    time->seconds = minutes * 60;
}

/* Original: budget portion of unnamed helper, file offset 0x68f0..0x6910.
 * Search flag initialization is handled by the search coordinator. Return the working
 * search budget with its three-second floor; do not overwrite the selected budget. */
int32_t bc_time_search_seconds(const BCThinkingTime *time) {
    return time->seconds < 3 ? 3 : time->seconds;
}
