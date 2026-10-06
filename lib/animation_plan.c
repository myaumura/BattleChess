#include "animation_plan.h"
#include <string.h>
#include <stdbool.h>

/* DOWALK 0xd3bc, STARTANI 0x133c4, WAITANIM 0x131f2.
 * Control flow and integer widths retain the recovered C ordering. */
typedef uint8_t byte;
typedef uint16_t ushort;
typedef uint8_t undefined1;
static const uint8_t display_to_engine[8] = {0, 4, 1, 5, 6, 2, 3, 0};

/* Native helper (no direct original address).
 * Purpose: Allocate one bounded native script node using the original eight-byte record layout. */
static uint8_t add(BCAnimationPlan *plan, uint8_t type, uint8_t source, uint8_t dest, uint8_t arg) {
    if (plan->count >= 32) {
        plan->count = 33;
        return 0;
    }
    uint8_t i = plan->count++;
    plan->nodes[i] = (BCAnimationNode){type, 2, 255, 255, source, dest, arg, 0};
    plan->last = i;
    return i;
}

/* Original: STARTANI, file offset 0x133c4.
 * Purpose: Create the root animation node. */
static uint8_t start(BCAnimationPlan *plan, uint8_t type, uint8_t source, uint8_t dest,
                     uint8_t arg) {
    plan->root = add(plan, type, source, dest, arg);
    return plan->root;
}

/* Original: WAITANIM, file offset 0x131f2.
 * Purpose: Attach successor work or append it to that successor parallel chain. */
static uint8_t after(BCAnimationPlan *plan, uint8_t parent, uint8_t type, uint8_t source,
                     uint8_t dest, uint8_t arg) {
    uint8_t i = add(plan, type, source, dest, arg);
    if (plan->count > 32)
        return i;
    if (plan->nodes[parent].next == 255)
        plan->nodes[parent].next = i;
    else {
        parent = plan->nodes[parent].next;
        while (plan->nodes[parent].parallel != 255)
            parent = plan->nodes[parent].parallel;
        plan->nodes[parent].parallel = i;
    }
    return i;
}

/* Original: DOWALK, file offset 0xd3bc.
 * Purpose: Build the ordinary-move choreography graph with original branch and node order. */
int bc_animation_plan_build(BCAnimationPlan *plan, const uint8_t board[64], BCMove move) {
    if (!plan || !board || move.special || (move.to & 0x88) || (move.from & 0x88) ||
        move.to > 0x77 || move.from > 0x77 || move.piece < 1 || move.piece > 6 || move.captured > 6)
        return 0;
    memset(plan, 0, sizeof(*plan));
    memcpy(plan->display, board, 64);
    unsigned source = (move.from & 7) | (((move.from >> 1) & 0x38) ^ 0x38);
    unsigned destination = (move.to & 7) | (((move.to >> 1) & 0x38) ^ 0x38);
    if (!board[source] || (move.captured && !board[destination]))
        return 0;
    ushort destination_work = move.to, source_work = move.from;
    uint8_t moving = move.piece, captured = move.captured;
    char defender_piece;
    byte source_square;
    ushort source_display, destination_display;
    bool special_path, walk_reaches_destination = true;
    byte destination_square, moving_piece_at_step, previous_square;
    ushort destination_saved, source_saved;
    undefined1 node_index;
    short steps_until_combat;
    byte knight_direction = 0, path_square, direction = 0;
    undefined1 defender_node, movement_node = 0;
    source_display = (source_work & 7) | ((((short)source_work >> 1) & 0x38U) ^ 0x38);
    destination_display =
        (destination_work & 7) | ((((short)destination_work >> 1) & 0x38U) ^ 0x38);
    source_work = source_display;
    source_square = (byte)source_work;
    destination_work = destination_display;
    destination_square = (byte)destination_work;
    if ((byte)source_work == (byte)destination_work) {
        return 0;
    } else {
        if (captured == '\0') {
            steps_until_combat = 200;
        } else {
            steps_until_combat = 0;
            path_square = source_square;
            while (path_square != (byte)destination_work) {
                if ((path_square & 0x38) < ((byte)destination_work & 0x38)) {
                    path_square = path_square + 8;
                } else if (((byte)destination_work & 0x38) < (path_square & 0x38)) {
                    path_square = path_square - 8;
                }
                if ((path_square & 7) < ((byte)destination_work & 7)) {
                    path_square = path_square + 1;
                } else if (((byte)destination_work & 7) < (path_square & 7)) {
                    path_square = path_square - 1;
                }
                steps_until_combat = steps_until_combat + 1;
            }
        }
        special_path = false;
        destination_saved = destination_display;
        source_saved = source_display;
        if (moving == 3) {
            special_path = true;
            destination_work = destination_display;
            source_work = source_display;
            if (((byte)source_work & 0x38) < ((byte)destination_work & 0x38)) {
                movement_node = start(plan, 6, source_display, 1, 0);
                destination_saved = destination_work;
                source_saved = source_work;
            } else if ((((byte)source_work & 0x38) == ((byte)destination_work & 0x38)) &&
                       ((board[(byte)source_work] & 0x40) == 0)) {
                movement_node = start(plan, 6, source_display, 1, 0);
                destination_saved = destination_work;
                source_saved = source_work;
            } else {
                movement_node = start(plan, 6, source_display, 0, 0);
                destination_saved = destination_work;
                source_saved = source_work;
            }
        }
        source_work = source_saved;
        destination_work = destination_saved;
        path_square = source_square;
        if (moving == 5) {
            steps_until_combat = steps_until_combat + -1;
            if ((source_square & 0x38) < (destination_square & 0x38)) {
                if ((source_square + 8 & 0x38) < (destination_square & 0x38)) {
                    if ((source_square & 7) < (destination_square & 7)) {
                        direction = 2;
                        path_square = source_square + 1;
                    } else {
                        direction = 6;
                        path_square = source_square - 1;
                    }
                } else {
                    direction = 4;
                    path_square = source_square + 8;
                }
            } else if ((destination_square & 0x38) < (source_square - 8 & 0x38)) {
                if ((source_square & 7) < (destination_square & 7)) {
                    direction = 2;
                    path_square = source_square + 1;
                } else {
                    direction = 6;
                    path_square = source_square - 1;
                }
            } else {
                direction = 0;
                path_square = source_square - 8;
            }
            node_index = start(plan, 2, source_display, direction, 0);
            if (board[path_square] != '\0') {
                after(plan, node_index, 8, path_square, direction, 0);
            }
            knight_direction = direction;
            movement_node = after(plan, node_index, 1, source_display, path_square, 0);
            special_path = true;
        }
        if ((path_square & 0x38) < (destination_square & 0x38)) {
            if ((path_square & 7) < (destination_square & 7)) {
                direction = 3;
            } else if ((destination_square & 7) < (path_square & 7)) {
                direction = 5;
            } else {
                direction = 4;
            }
        } else if ((destination_square & 0x38) < (path_square & 0x38)) {
            if ((path_square & 7) < (destination_square & 7)) {
                direction = 1;
            } else if ((destination_square & 7) < (path_square & 7)) {
                direction = 7;
            } else {
                direction = 0;
            }
        } else if ((path_square & 7) < (destination_square & 7)) {
            direction = 2;
        } else if ((destination_square & 7) < (path_square & 7)) {
            direction = 6;
        }
        if (moving == 6) {
            if (direction == 2) {
                direction = 1;
            } else if (direction == 6) {
                direction = 7;
            }
        }
        if (special_path) {
            movement_node = after(plan, movement_node, 2, source_display, direction, 0);
        } else {
            movement_node = start(plan, 2, source_display, direction, 0);
        }
        if (steps_until_combat < 2) {
            defender_piece = display_to_engine[(byte)board[destination_square] & 7];
            if (direction == 1) {
                if (defender_piece == '\x04') {
                    movement_node = after(plan, movement_node, 2, destination_display, 5, 0);
                    node_index = after(plan, movement_node, 4, destination_display, 6, 0);
                    defender_node = after(plan, node_index, 2, destination_display, 3, 0);
                } else {
                    if (defender_piece == '\x03') {
                        movement_node = after(plan, movement_node, 6, destination_display, 1, 0);
                    }
                    movement_node = after(plan, movement_node, 2, destination_display, 6, 0);
                    node_index = after(plan, movement_node, 4, destination_display, 6, 0);
                    defender_node = after(plan, node_index, 2, destination_display, 4, 0);
                }
                defender_node =
                    after(plan, defender_node, 7, destination_display, destination_display, 0);
            } else {
                if (defender_piece == '\x03') {
                    movement_node = after(plan, movement_node, 6, destination_display, 1, 0);
                } else if (defender_piece == '\x05') {
                    movement_node = after(plan, movement_node, 2, destination_display, 4, 0);
                } else {
                    movement_node = after(plan, movement_node, 2, destination_display, 5, 0);
                }
                defender_node =
                    after(plan, movement_node, 7, destination_display, destination_display, 0);
            }
            if ((defender_piece == '\x03') || (defender_piece == '\x05')) {
                defender_node = after(plan, defender_node, 2, destination_display, 0, 0);
            } else {
                defender_node = after(plan, defender_node, 2, destination_display, 1, 0);
            }
            node_index = after(plan, defender_node, 3, destination_display, destination_display, 0);
            after(plan, node_index, 5, destination_display, 0, 0);
            steps_until_combat = 200;
        }
        while (previous_square = path_square, moving_piece_at_step = moving,
               path_square != destination_square) {
            if ((path_square & 0x38) < (destination_square & 0x38)) {
                path_square = path_square + 8;
            } else if ((destination_square & 0x38) < (path_square & 0x38)) {
                path_square = path_square - 8;
            }
            if ((path_square & 7) < (destination_square & 7)) {
                path_square = path_square + 1;
            } else if ((destination_square & 7) < (path_square & 7)) {
                path_square = path_square - 1;
            }
            if (moving == 5) {
                if ((board[path_square] != '\0') && (path_square != destination_square)) {
                    after(plan, movement_node, 8, path_square, knight_direction, 0);
                }
                if (board[previous_square] != '\0') {
                    after(plan, movement_node, 8, previous_square, knight_direction + 4 & 7, 0);
                }
            }
            if ((((board[path_square] == '\0') || (path_square != destination_square)) ||
                 (direction < 4)) ||
                (6 < direction)) {
                special_path = false;
            } else {
                special_path = true;
            }
            if (special_path) {
                walk_reaches_destination = false;
            } else {
                movement_node = after(plan, movement_node, 1, source_display, path_square, 0);
            }
            steps_until_combat = steps_until_combat + -1;
            if (steps_until_combat < 2) {
                defender_piece = display_to_engine[(byte)board[destination_square] & 7];
                if (direction == 1) {
                    if (defender_piece == '\x04') {
                        movement_node = after(plan, movement_node, 2, destination_display, 5, 0);
                        node_index = after(plan, movement_node, 4, destination_display, 6, 0);
                        defender_node = after(plan, node_index, 2, destination_display, 3, 0);
                    } else {
                        if (defender_piece == '\x03') {
                            movement_node =
                                after(plan, movement_node, 6, destination_display, 1, 0);
                        }
                        movement_node = after(plan, movement_node, 2, destination_display, 6, 0);
                        node_index = after(plan, movement_node, 4, destination_display, 6, 0);
                        defender_node = after(plan, node_index, 2, destination_display, 4, 0);
                    }
                    defender_node =
                        after(plan, defender_node, 7, destination_display, destination_display, 0);
                } else {
                    if (defender_piece == '\x03') {
                        movement_node = after(plan, movement_node, 6, destination_display, 1, 0);
                    } else if (defender_piece == '\x05') {
                        movement_node = after(plan, movement_node, 2, destination_display, 4, 0);
                    } else {
                        movement_node = after(plan, movement_node, 2, destination_display, 5, 0);
                    }
                    defender_node =
                        after(plan, movement_node, 7, destination_display, destination_display, 0);
                }
                if ((defender_piece == '\x03') || (defender_piece == '\x05')) {
                    defender_node = after(plan, defender_node, 2, destination_display, 0, 0);
                } else {
                    defender_node = after(plan, defender_node, 2, destination_display, 1, 0);
                }
                node_index =
                    after(plan, defender_node, 3, destination_display, destination_display, 0);
                after(plan, node_index, 5, destination_display, 0, 0);
                steps_until_combat = 200;
            }
        }
        if (captured != '\0') {
            if (walk_reaches_destination) {
                if ((moving == 3) || (moving == 5)) {
                    movement_node = after(plan, movement_node, 2, source_display, 0, 0);
                } else {
                    movement_node = after(plan, movement_node, 2, source_display, 1, 0);
                }
            }
            node_index = after(plan, movement_node, 7, source_display, destination_display, 1);
            if ((moving_piece_at_step == 3) || (moving_piece_at_step == 5)) {
                movement_node = after(plan, node_index, 2, source_display, 4, 0);
                direction = 4;
            } else {
                movement_node = after(plan, node_index, 2, source_display, 5, 0);
            }
            node_index = after(plan, movement_node, 3, source_display, source_display, 0);
            node_index = after(plan, node_index, 5, source_display, 1, 0);
            movement_node = after(plan, node_index, 7, source_display, destination_display, 2);
        }
        if (moving == 3) {
            if ((direction == 2) || (direction == 6)) {
                if ((board[source_square] & 0x40) == 0) {
                    node_index = after(plan, movement_node, 2, source_display, 4, 0);
                    movement_node = after(plan, node_index, 6, source_display, 3, 0);
                } else {
                    node_index = after(plan, movement_node, 2, source_display, 0, 0);
                    movement_node = after(plan, node_index, 6, source_display, 2, 0);
                }
            } else if (direction == 0) {
                movement_node = after(plan, movement_node, 6, source_display, 2, 0);
            } else {
                movement_node = after(plan, movement_node, 6, source_display, 3, 0);
            }
        } else if (moving == 4) {
            if ((direction == 0) || (3 < direction)) {
                if ((board[source_square] & 0x40) == 0) {
                    movement_node = after(plan, movement_node, 2, source_display, 5, 0);
                } else {
                    movement_node = after(plan, movement_node, 2, source_display, 7, 0);
                }
            } else if ((board[source_square] & 0x40) == 0) {
                movement_node = after(plan, movement_node, 2, source_display, 3, 0);
            } else {
                movement_node = after(plan, movement_node, 2, source_display, 1, 0);
            }
        } else if ((board[source_square] & 0x40) == 0) {
            movement_node = after(plan, movement_node, 2, source_display, 4, 0);
        } else {
            movement_node = after(plan, movement_node, 2, source_display, 0, 0);
        }
        after(plan, movement_node, 3, source_display, destination_display, 0);
    }
    return plan->count <= 32;
}

/* Original: DOWALK, file offset 0xd3bc.
 * Purpose: Translate its special-move prelude into sequential ordinary moves. */
size_t bc_animation_split_move(BCMove move, BCMove out[2]) {
    if (!out)
        return 0;
    if (!move.special) {
        out[0] = move;
        return 1;
    }
    if (move.piece != 1 && move.piece != 6) {
        move.special = 0;
        move.piece = 6;
        out[0] = move;
        return 1;
    }
    if (move.piece == 6) {
        unsigned intermediate = (move.from & 0x70) | (move.to & 7);
        out[0] = (BCMove){(uint16_t)intermediate, move.from, 0, 6, 6};
        out[1] = (BCMove){move.to, (uint16_t)intermediate, 0, 6, 0};
    } else {
        out[0] = move;
        out[0].special = 0;
        out[0].captured = 0;
        out[1] = (BCMove){(uint16_t)((move.to & 0xf0) | ((move.to & 7) == 6 ? 5 : 3)),
                          (uint16_t)((move.from & 0xf0) | ((move.to & 7) == 6 ? 7 : 0)), 0, 3, 0};
    }
    return 2;
}

/* BUILDCHE 0x739e and DOCHECKM 0x716a. The original null record at
 * 0xf99d8 is {8,8,0,0,0}; moving piece zero suppresses DOWALK. */
/* Original: BUILDCHE, file offset 0x739e.
 * Purpose: Choose the presentation-only king capture used by DOCHECKM (0x716a). */
int bc_animation_checkmate_move(const BCGame *game, BCMove *out) {
    if (!game || !out || game->position.side > 1 || game->position.opponent > 1)
        return 0;
    *out = (BCMove){8, 8, 0, 0, 0};
    if (!game->historyCount || !bcGameInCheck(game))
        return 0;
    const Position *position = &game->position;
    unsigned target = position->pieces[position->side][0].square, side = position->opponent;
    BCMove last = game->history[game->historyCount - 1];
    if (!last.piece)
        return 0;
    if (bcGamePieceAttacks(position, last.piece, side, last.to, target)) {
        *out = (BCMove){(uint16_t)target, last.to, 0, last.piece, 1};
        return 1;
    }
    for (int i = 1; i <= position->last_piece[side] && i < 16; i++) {
        PieceEntry entry = position->pieces[side][i];
        if (entry.piece &&
            bcGamePieceAttacks(position, entry.piece, side, entry.square, target)) {
            *out = (BCMove){(uint16_t)target, entry.square, 0, entry.piece, 1};
            return 1;
        }
    }
    return 0;
}
