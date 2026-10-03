#include "animation.h"
#include <assert.h>
#include <stdio.h>
static const uint8_t fast[] = {255};
static const uint8_t sound_time[] = {0x82, 0x43, 1, 255};
static int sounds;
// Native validation (no original entrypoint): Supply deterministic synthetic clips to the animation
// runtime.
static BCAnimClip get_clip(void *v, int group, int seq) {
    (void)v;
    (void)group;
    return (BCAnimClip){100 + (unsigned)seq * 10, 6};
}
// Native validation (no original entrypoint): Supply a terminal timing stream for immediate
// animation steps.
static const uint8_t *get_time(void *v, int type, int index) {
    (void)v;
    (void)type;
    (void)index;
    return fast;
}
// Native validation (no original entrypoint): Supply a fixed standing frame for sprite assertions.
static uint32_t standing(void *v, uint8_t b, int f) {
    (void)v;
    (void)b;
    (void)f;
    return 900;
}
// Native validation (no original entrypoint): Check decoded sound parameters and count callbacks.
static void sound(void *v, unsigned i, unsigned c, int p) {
    (void)v;
    assert(i == 2 && c == 1 && p == 3);
    ++sounds;
}
// Native validation (no original entrypoint): Hide the requested fixture sprite to simulate the
// host fade callback.
static void fade(void *v, unsigned i) {
    BCAnimation *a = v;
    a->sprites[i].visible = 0;
}
// Native validation (no original entrypoint): Prepare an animation fixture with deterministic asset
// callbacks.
static void setup(BCAnimation *a, int type) {
    bc_animation_reset(a);
    a->assets.context = a;
    a->assets.clip = get_clip;
    a->assets.timing = get_time;
    a->assets.standing = standing;
    a->assets.sound = sound;
    a->assets.fade = fade;
    for (int i = 0; i < 64; i++)
        a->anchors_x[i] = i % 8 * 20;
    for (int i = 0; i < 8; i++)
        a->anchors_y[i] = i * 10;
    a->board[0] = 66;
    a->sprite_at[0] = 0;
    a->sprites[0].color = 1;
    a->sprites[0].visible = 1;
    a->nodes[0] = (BCAnimNode){type, 2, 255, 255, 0, 1, 2, 0};
}
// Native validation (no original entrypoint): Advance a fixture with a bounded loop and require
// completion.
static void finish(BCAnimation *a) {
    int n;
    for (n = 1; n < 100; n++) {
        int r = bc_animation_step(a, n * 6);
        assert(r >= 0);
        if (r)
            break;
    }
    assert(n < 100);
}
// Native validation (no original entrypoint): Run the animation check regression assertions.
int main(void) {
    assert(bc_animation_coordinate(10, 0, 6, 1) == 8);
    assert(bc_animation_coordinate(0, 10, 6, 1) == 2);
    BCAnimation a;
    setup(&a, BC_WALK);
    assert(!bc_animation_activate(&a, 0));
    assert(!bc_animation_step(&a, 5));
    assert(!a.active[0].started);
    assert(!bc_animation_step(&a, 6));
    assert(a.sprites[0].frame == 100 && a.sprites[0].x == 0);
    assert(!bc_animation_step(&a, 12));
    assert(a.sprites[0].frame == 101 && a.sprites[0].x == 3);
    finish(&a);
    assert(a.sprites[0].target_x == 20);
    setup(&a, BC_WALK);
    bc_animation_activate(&a, 0);
    a.active[0].timing = sound_time;
    bc_animation_step(&a, 6);
    assert(sounds == 1 && a.active[0].delay == 1);
    bc_animation_step(&a, 12);
    assert(a.sprites[0].frame == 100);
    bc_animation_step(&a, 18);
    assert(a.sprites[0].frame == 101);
    setup(&a, BC_HALF_WALK);
    a.assets.offset_x[1] = 9;
    bc_animation_activate(&a, 0);
    finish(&a);
    assert(a.sprites[0].target_x == 9 && a.sprites[0].frame == 103);
    setup(&a, BC_SLIDE);
    a.assets.offset_y[1] = -7;
    bc_animation_activate(&a, 0);
    finish(&a);
    assert(a.sprites[0].y == -7 && a.sprites[0].target_y == -7);
    setup(&a, BC_APPROACH);
    bc_animation_activate(&a, 0);
    finish(&a);
    assert(a.sprites[0].target_x == 20 && a.sprites[0].frame == 102);
    setup(&a, BC_TRANSFORM);
    bc_animation_activate(&a, 0);
    finish(&a);
    assert(a.sprites[0].frame == 255);
    setup(&a, BC_TURN);
    bc_animation_activate(&a, 0);
    finish(&a);
    assert(((a.board[0] >> 3) & 7) == 1);
    setup(&a, BC_STAND);
    bc_animation_activate(&a, 0);
    finish(&a);
    assert(a.board[0] == 0 && a.board[1] == 66 && a.sprites[0].frame == 900);
    static const BCAnimClip combat[] = {{300, 2}, {400, 2}, {0, 0}};
    setup(&a, BC_COMBAT);
    a.nodes[0].dest = 0;
    a.nodes[0].parallel = 1;
    a.nodes[1] = (BCAnimNode){BC_COMBAT, 2, 255, 255, 1, 1, 0, 0};
    a.board[1] = 2;
    a.sprite_at[1] = 1;
    a.sprites[1].square = 1;
    a.assets.combat[0] = a.assets.combat[1] = combat;
    a.assets.combat_timing[0] = a.assets.combat_timing[1] = fast;
    bc_animation_activate(&a, 0);
    finish(&a);
    assert(a.combat_state == 4 && a.sprites[0].frame == 401 && a.sprites[1].target_x == 35);
    setup(&a, BC_WALK);
    a.flat = 1;
    bc_animation_activate(&a, 0);
    finish(&a);
    assert(a.sprites[0].target_x == 20);
    /* FREEANIM slot reuse changes the current record's next link before traversal. */
    setup(&a, BC_STAND);
    a.nodes[0].dest = 0;
    a.nodes[0].parallel = 1;
    a.nodes[0].next = 2;
    a.nodes[1] = (BCAnimNode){BC_WALK, 2, 255, 255, 1, 2, 0, 0};
    a.nodes[2] = (BCAnimNode){BC_STAND, 2, 255, 255, 0, 0, 0, 0};
    a.board[1] = 66;
    a.sprite_at[1] = 1;
    a.sprites[1].square = 1;
    bc_animation_activate(&a, 0);
    bc_animation_step(&a, 6);
    assert(a.nodes[0].status == 0 && a.nodes[2].status == 1 && !a.active[1].started);
    finish(&a);
    assert(a.sprites[1].target_x == 40);
    puts("animation handlers: ok");
}
