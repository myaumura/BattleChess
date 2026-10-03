#ifndef BC_ANIMATION_H
#define BC_ANIMATION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Original node numbers, not asset sequence numbers. */
enum {
    BC_WALK = 1,
    BC_TURN,
    BC_STAND,
    BC_HALF_WALK,
    BC_COMBAT,
    BC_TRANSFORM,
    BC_APPROACH,
    BC_SLIDE
};
typedef struct {
    uint32_t first;
    uint16_t count;
} BCAnimClip;
typedef struct {
    uint8_t type, status, next, parallel, source, dest, arg, pad;
} BCAnimNode;
typedef struct {
    int16_t x, y, target_x, target_y;
    uint32_t frame, order;
    uint8_t color, square, visible;
} BCAnimSprite;
typedef struct {
    void *context;
    BCAnimClip (*clip)(void *, int group, int sequence);
    const uint8_t *(*timing)(void *, int display_type, int index);
    uint32_t (*standing)(void *, uint8_t display_byte, int flat);
    void (*sound)(void *, unsigned index, unsigned color, int parameter);
    void (*fade)(void *, unsigned sprite);
    const BCAnimClip *combat[2]; /* count==0 terminates */
    const uint8_t *combat_timing[2];
    int8_t offset_x[8], offset_y[8];
} BCAnimAssets;
typedef struct {
    BCAnimClip clip;
    const uint8_t *timing;
    const BCAnimClip *combat;
    int16_t sx, sy, tx, ty, turn;
    uint16_t frame;
    uint8_t node, type, sprite, delay, n, k, orientation, target, type_id, side, whole, started,
        finished;
    int next;
} BCAnimActive;
typedef struct {
    BCAnimAssets assets;
    BCAnimNode nodes[32];
    BCAnimSprite sprites[32];
    uint8_t board[64], sprite_at[64];
    int16_t anchors_x[64], anchors_y[8];
    BCAnimActive active[4];
    int pool[4], used, head;
    uint32_t previous_ticks, order;
    uint8_t flat, combat_state, error;
} BCAnimation;
/* Caller fills assets/nodes/board/sprites/sprite_at/anchors after reset. */
void bc_animation_reset(BCAnimation *a);
int bc_animation_activate(BCAnimation *a, unsigned node);
/* 1 only on a scheduler pass that begins with no active nodes; -1 invalid data. */
int bc_animation_step(BCAnimation *a, uint32_t ticks);
int16_t bc_animation_coordinate(int16_t start, int16_t end, int16_t n, int16_t k);
#ifdef __cplusplus
}
#endif
#endif
