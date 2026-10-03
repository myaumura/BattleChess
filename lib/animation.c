#include "animation.h"
#include <string.h>

/* Original: LINECCFW, file offset 0x130ce.
 * Purpose: Interpolate one coordinate with signed word truncation and rounding. */
int16_t bc_animation_coordinate(int16_t start, int16_t end, int16_t steps, int16_t step) {
    int16_t delta = (int16_t)(end - start);
    if (!steps)
        return start;
    int16_t remainder = (int16_t)((delta % steps) * step + (delta < 0 ? -steps / 2 : steps / 2));
    return (int16_t)(start + (delta / steps) * step + remainder / steps);
}
/* Native helper (no direct original address).
 * Purpose: Initialize native scheduler storage and the original four-record pool layout. */
void bc_animation_reset(BCAnimation *animation) {
    memset(animation, 0, sizeof(*animation));
    animation->head = -1;
    for (int i = 0; i < 4; i++) {
        animation->pool[i] = i;
        animation->active[i].next = -1;
    }
    for (int i = 0; i < 32; i++)
        animation->nodes[i].next = animation->nodes[i].parallel = 255;
    memset(animation->sprite_at, 255, sizeof(animation->sprite_at));
}
/* Native helper (no direct original address).
 * Purpose: Resolve animation decoded animation clip through the host asset callback. */
static BCAnimClip clip(BCAnimation *animation, int group, int sequence) {
    BCAnimClip result = {0, 0};
    if (animation->assets.clip)
        result = animation->assets.clip(animation->assets.context, group, sequence);
    if (!result.count)
        animation->error = 1;
    return result;
}
/* Native helper (no direct original address).
 * Purpose: Resolve the timing stream selected by ACTIVATE (file offset 0x134cc). */
static const uint8_t *timing(BCAnimation *animation, int type, int index) {
    const uint8_t *stream = animation->assets.timing
                                ? animation->assets.timing(animation->assets.context, type, index)
                                : 0;
    if (!stream && !animation->flat)
        animation->error = 1;
    return stream;
}
/* Original: CHECKTIM, file offset 0x15956.
 * Purpose: Consume frame delays and dispatch embedded sound commands. */
static int timed(BCAnimation *animation, BCAnimActive *active) {
    if (active->delay) {
        --active->delay;
        return 0;
    }
    if (animation->flat)
        return 1;
    if (!active->timing) {
        animation->error = 1;
        return 0;
    }
    while (*active->timing > 127 && *active->timing != 255) {
        if (animation->assets.sound)
            animation->assets.sound(animation->assets.context, *active->timing & 15,
                                    animation->sprites[active->sprite].color,
                                    (int)active->timing[1] - 64);
        active->timing += 2;
    }
    if (*active->timing != 255)
        active->delay = *active->timing++;
    return 1;
}
/* Native helper (no direct original address).
 * Purpose: Mirror RENDERSH coordinates (0x9c74) and ANIMSHAP ordering (0x9bc4) in native sprite
 * state. */
static void position(BCAnimation *animation, BCAnimSprite *sprite, int x, int y, uint32_t frame) {
    sprite->x = sprite->target_x = (int16_t)x;
    sprite->y = sprite->target_y = (int16_t)y;
    sprite->frame = frame;
    sprite->visible = 1;
    sprite->color = sprite->color != 0;
    sprite->order = ++animation->order;
}
/* Native helper (no direct original address).
 * Purpose: Apply LINECCFW (file offset 0x130ce) to both sprite coordinates. */
static void interpolate(BCAnimation *animation, BCAnimSprite *sprite, BCAnimActive *active, int k,
                        uint32_t frame) {
    position(animation, sprite, bc_animation_coordinate(active->sx, active->tx, active->n, k),
             bc_animation_coordinate(active->sy, active->ty, active->n, k), frame);
}
/* Native helper (no direct original address).
 * Purpose: Append an active record in ACTIVATE order (file offset 0x134cc). */
static void append(BCAnimation *animation, int slot) {
    int *link = &animation->head;
    while (*link >= 0)
        link = &animation->active[*link].next;
    *link = slot;
    animation->active[slot].next = -1;
}
/* Native helper (no direct original address).
 * Purpose: Unlink animation completed record and return its slot to the original stack pool. */
static void remove_active(BCAnimation *animation, int slot) {
    int *link = &animation->head;
    while (*link >= 0 && *link != slot)
        link = &animation->active[*link].next;
    if (*link < 0) {
        animation->error = 1;
        return;
    }
    *link = animation->active[slot].next;
    animation->pool[--animation->used] = slot;
}
/* Original: ACTIVATE, file offset 0x134cc.
 * Purpose: Prepare animation node, append it, and activate linked parallel work. */
int bc_animation_activate(BCAnimation *animation, unsigned index) {
    if (index >= 32 || animation->used == 4) {
        animation->error = 1;
        return -1;
    }
    BCAnimNode *node = &animation->nodes[index];
    unsigned source = node->source, destination = node->dest;
    if (source >= 64 || animation->sprite_at[source] >= 32 || node->type < 1 || node->type > 8) {
        animation->error = 1;
        return -1;
    }
    int slot = animation->pool[animation->used++];
    BCAnimActive *active = &animation->active[slot];
    memset(active, 0, sizeof(*active));
    active->next = -1;
    active->node = index;
    active->type = node->type;
    active->sprite = animation->sprite_at[source];
    active->type_id = animation->board[source] & 7;
    active->orientation = (animation->board[source] >> 3) & 7;
    active->target = destination;
    BCAnimSprite *sprite = &animation->sprites[active->sprite];
    node->status = 1;
    int skip = 0,
        half_orientations = active->type_id == 1 || active->type_id == 3 || active->type_id == 6;
    if (animation->flat && (active->type == BC_TURN || active->type == BC_HALF_WALK ||
                            active->type == BC_TRANSFORM || active->type == BC_COMBAT)) {
        if (active->type == BC_COMBAT)
            animation->combat_state = 1;
        skip = 1;
    }
    if (animation->flat && active->type == BC_APPROACH) {
        if (source == destination || node->arg < 2)
            skip = 1;
        else
            active->type = BC_WALK;
    }
    if (!skip)
        switch (active->type) {
        case BC_WALK:
            if (destination >= 64)
                goto invalid;
            active->sx = animation->anchors_x[sprite->square];
            active->sy = animation->anchors_y[sprite->square >> 3];
            active->tx = animation->anchors_x[destination];
            active->ty = animation->anchors_y[destination >> 3];
            sprite->square = destination;
            active->clip = clip(animation, animation->flat ? -1 : active->type_id - 1,
                                animation->flat ? active->type_id + 16
                                                : (half_orientations ? active->orientation >> 1
                                                                     : active->orientation));
            active->timing = timing(animation, active->type_id, active->orientation);
            break;
        case BC_HALF_WALK:
        case BC_SLIDE:
            if (destination >= 8)
                goto invalid;
            active->tx = animation->assets.offset_x[destination];
            active->ty = animation->assets.offset_y[destination];
            if (active->type == BC_HALF_WALK) {
                active->clip =
                    clip(animation, active->type_id - 1,
                         half_orientations ? active->orientation >> 1 : active->orientation);
                active->timing = timing(animation, active->type_id, active->orientation);
            }
            break;
        case BC_APPROACH:
            if (destination >= 64)
                goto invalid;
            sprite->square = destination;
            active->tx = animation->anchors_x[destination];
            active->ty = animation->anchors_y[destination >> 3];
            if (node->arg == 1) {
                active->tx += 15;
                --active->ty;
                active->whole = active->orientation > 3 && active->orientation < 7;
            } else if (node->arg == 0) {
                active->tx -= 25;
                active->ty += 13;
            }
            active->clip = clip(animation, active->type_id - 1,
                                half_orientations ? active->orientation >> 1 : active->orientation);
            active->timing = timing(animation, active->type_id, active->orientation);
            break;
        case BC_TURN:
            animation->board[source] = (animation->board[source] & 0x47) | (destination << 3);
            break;
        case BC_STAND:
            if (destination >= 64)
                goto invalid;
            active->side = 255;
            if (source != destination) {
                if (animation->combat_state) {
                    animation->combat_state = 0;
                    active->side = animation->sprite_at[destination];
                    if (animation->flat && active->side < 32)
                        animation->sprites[active->side].visible = 0;
                }
                animation->board[destination] = animation->board[source];
                animation->board[source] = 0;
                animation->sprite_at[destination] = animation->sprite_at[source];
            }
            break;
        case BC_TRANSFORM:
            active->clip = clip(animation, active->type_id - 1, destination + 14);
            animation->board[source] =
                (animation->board[source] & 0x47) |
                (destination < 2 ? destination << 5 : (sprite->x < 161 ? 24 : 56));
            break;
        case BC_COMBAT:
            if (destination > 1)
                goto invalid;
            active->side = destination;
            active->combat = animation->assets.combat[destination];
            active->timing = animation->assets.combat_timing[destination];
            if (!active->combat || !active->timing)
                goto invalid;
            ++animation->combat_state;
            break;
        }
    if (!skip)
        append(animation, slot);
    if (node->parallel != 255)
        bc_animation_activate(animation, node->parallel);
    if (skip) {
        node->status = 0;
        animation->pool[--animation->used] = slot;
        if (node->next != 255)
            bc_animation_activate(animation, node->next);
    }
    return animation->error ? -1 : 0;
invalid:
    animation->error = 1;
    return -1;
}
/* Native helper (no direct original address).
 * Purpose: Dispatch translated HANDLEWA (0x14244), HANDLETU (0x151bc), HANDLEST (0x1553c), HANDLEHW
 * (0x14576), HANDLECO (0x155f2), HANDLETR (0x1508c), HANDLECP (0x14e4a), and HANDLESL (0x147a2). */
static int handle(BCAnimation *animation, BCAnimActive *active) {
    BCAnimSprite *sprite = &animation->sprites[active->sprite];
    if (active->type == BC_STAND) {
        if (!animation->assets.standing) {
            animation->error = 1;
            return 0;
        }
        position(animation, sprite, sprite->target_x, sprite->target_y,
                 animation->assets.standing(animation->assets.context,
                                            animation->board[active->target], animation->flat));
        if (active->side != 255 && !animation->flat) {
            if (animation->assets.fade)
                animation->assets.fade(animation->assets.context, active->side);
            else
                animation->error = 1;
        }
        return 1;
    }
    if (active->type == BC_COMBAT) {
        if (animation->combat_state == 1)
            return 0;
        if (active->finished) {
            if (animation->combat_state != 4)
                return 0;
            if (active->side) {
                sprite->target_x = animation->anchors_x[sprite->square] + 15;
                sprite->target_y = animation->anchors_y[sprite->square >> 3] - 1;
            }
            return 1;
        }
        if (!timed(animation, active))
            return 0;
        if (!active->started) {
            active->clip = *active->combat++;
            if (!active->clip.count) {
                active->finished = 1;
                ++animation->combat_state;
                return 0;
            }
            active->frame = 0;
            active->started = 1;
            position(animation, sprite, animation->anchors_x[sprite->square],
                     animation->anchors_y[sprite->square >> 3] + (active->side ? 0 : 1),
                     active->clip.first);
        } else
            position(animation, sprite, sprite->x, sprite->y, active->clip.first + active->frame);
        if (++active->frame == active->clip.count)
            active->started = 0;
        return 0;
    }
    if (active->type == BC_TURN) {
        if (active->turn && !timed(animation, active))
            return 0;
        if (!active->started) {
            if (!active->turn) {
                if (active->target == active->orientation)
                    return 1;
                active->turn = ((active->target - active->orientation) & 4) ? -1 : 1;
                if (active->type_id == 1 || active->type_id == 3 || active->type_id == 6)
                    active->turn *= 2;
            } else {
                unsigned next = (active->orientation + active->turn) & 7;
                if (next == active->target)
                    return 1;
                active->orientation = next;
            }
            unsigned sequence = active->orientation + (active->turn < 0 ? 16 : 8);
            if (active->type_id == 1 || active->type_id == 3 || active->type_id == 6)
                sequence >>= 1;
            active->clip = clip(animation, active->type_id - 1, sequence);
            active->frame = 0;
            active->started = 1;
            position(animation, sprite, active->turn && active->k ? sprite->x : sprite->target_x,
                     active->turn && active->k ? sprite->y : sprite->target_y, active->clip.first);
            active->k = 1;
            active->timing = timing(animation, active->type_id,
                                    8 + active->orientation * 2 + (active->turn >= 0));
            active->delay = 0;
            timed(animation, active);
        } else
            position(animation, sprite, sprite->x, sprite->y, active->clip.first + active->frame);
        if (++active->frame == active->clip.count)
            active->started = 0;
        return 0;
    }
    if (active->type == BC_SLIDE || active->type == BC_TRANSFORM) {
        if (active->delay) {
            --active->delay;
            return 0;
        }
    } else if (!timed(animation, active))
        return 0;
    if (active->type == BC_TRANSFORM) {
        position(animation, sprite, active->started ? sprite->x : sprite->target_x,
                 active->started ? sprite->y : sprite->target_y,
                 active->clip.first + active->frame);
        active->started = 1;
        return ++active->frame == active->clip.count;
    }
    if (!active->started) {
        active->started = 1;
        active->frame = 0;
        if (active->type == BC_WALK) {
            active->n = animation->flat ? 16 : active->clip.count;
            active->k = 1;
            position(animation, sprite, active->sx, active->sy, active->clip.first);
            return 0;
        }
        active->sx = sprite->target_x;
        active->sy = sprite->target_y;
        if (active->type == BC_SLIDE || active->type == BC_HALF_WALK) {
            active->tx += active->sx;
            active->ty += active->sy;
        }
        active->n = active->type == BC_SLIDE
                        ? 6
                        : (active->whole ? active->clip.count : active->clip.count / 2);
        active->k = 0;
        if (!active->n) {
            animation->error = 1;
            return 0;
        }
        if (active->type == BC_HALF_WALK) {
            active->k = 1;
            position(animation, sprite, active->sx, active->sy, active->clip.first);
            active->frame = 1;
            if (active->k == active->n + 1) {
                sprite->target_x = active->tx;
                sprite->target_y = active->ty;
                return 1;
            }
            return 0;
        }
    }
    if (active->type == BC_WALK) {
        if (active->k >= active->n) {
            sprite->target_x = active->tx;
            sprite->target_y = active->ty;
            return 1;
        }
        ++active->frame;
    }
    uint32_t frame = active->type == BC_SLIDE
                         ? sprite->frame
                         : active->clip.first + (animation->flat ? 0 : active->frame);
    interpolate(animation, sprite, active, active->k++, frame);
    if (active->type != BC_WALK)
        ++active->frame;
    if ((active->type == BC_SLIDE || active->type == BC_HALF_WALK) && active->k == active->n + 1) {
        sprite->target_x = active->tx;
        sprite->target_y = active->ty;
        return 1;
    }
    if (active->type == BC_APPROACH && active->k == active->n) {
        sprite->target_x = active->tx;
        sprite->target_y = active->ty;
        return 1;
    }
    return 0;
}
/* Original: HANDLEAL, file offset 0x14048.
 * Purpose: Advance active nodes once per six ticks, preserving pool-reuse traversal. */
int bc_animation_step(BCAnimation *animation, uint32_t ticks) {
    if (animation->error)
        return -1;
    if ((int32_t)(ticks - animation->previous_ticks) < 6)
        return 0;
    animation->previous_ticks = ticks;
    if (animation->head < 0)
        return 1;
    for (int slot = animation->head; slot >= 0; slot = animation->active[slot].next) {
        if (handle(animation, &animation->active[slot])) {
            unsigned node = animation->active[slot].node;
            animation->nodes[node].status = 0;
            remove_active(animation, slot);
            if (animation->nodes[node].next != 255)
                bc_animation_activate(animation, animation->nodes[node].next);
        }
        if (animation->error)
            return -1;
    }
    return 0;
}
