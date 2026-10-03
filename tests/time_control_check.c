#include "time_control.h"
#include <assert.h>
#include <stdio.h>

/* Native recovery check; no original entry point. Assert SETLEVEL/SETTIME
 * boundaries from file offsets 0x6838/0x381e, including the three-second floor. */
int main(void) {
    BCThinkingTime time = {0, 6};
    assert(bc_time_select_level(&time, 0) && time.seconds == 6);
    for (unsigned level = 1; level <= 9; ++level) {
        assert(bc_time_select_level(&time, level));
        assert(time.level == level && time.seconds == (5 << (level - 1)));
    }
    assert(!bc_time_select_level(&time, 10));
    assert(!bc_time_select_level(&time, 255));
    assert(time.level == 9 && time.seconds == 1280);
    assert(bc_time_dialog_minutes(&time) == 21);
    bc_time_set_minutes(&time, -10);
    assert(time.level == 10 && time.seconds == 60);
    bc_time_set_minutes(&time, 0);
    assert(time.seconds == 60);
    bc_time_set_minutes(&time, 17);
    assert(time.seconds == 1020);
    bc_time_set_minutes(&time, 10001);
    assert(time.seconds == 600000);
    time.seconds = 6;
    assert(bc_time_dialog_minutes(&time) == 1 && time.seconds == 60);
    time.seconds = 2;
    assert(bc_time_search_seconds(&time) == 3);
    time.seconds = 60;
    assert(bc_time_search_seconds(&time) == 60);
    puts("Original level budgets, custom-minute clamp and search floor passed");
}
