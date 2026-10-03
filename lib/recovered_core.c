/* Hand translation from dump.log; offsets are FILE offsets, not runtime PCs.
 * This is compilable explanatory C, NOT a complete or playable Battle Chess.
 * A5 globals become explicit state. Integers are host values, not a mapped
 * big-endian memory image. No original engine, UI or sound code is invented.
 * Run: clang -std=c11 -Wall -Wextra -Werror -DBC_SELF_TEST lib/recovered_core.c
 *      -o /tmp/bc-core && /tmp/bc-core
 */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "recovered_core.h"

static_assert(sizeof(Square) == 4, "square layout");
static_assert(sizeof(PieceEntry) == 2, "piece-list layout");

/* CALCSQUA, 0x666e..0x66c6. Original calls a character-table lowercase
 * helper at 0x1a520. ASCII case conversion suffices for chess coordinates.
 * DELIBERATE FIX: original returns an uninitialized local on invalid input;
 * this version returns -1. Non-ASCII table behavior is not reproduced. */
/* Original: CALCSQUA, file offset 0x666e.
 * Purpose: Convert algebraic coordinates to a 0x88 square. */
int calc_square(unsigned char file, unsigned char rank) {
    if (file >= 'A' && file <= 'Z')
        file += 'a' - 'A';
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8')
        return -1;
    return (rank - '1') * 16 + file - 'a';
}

/* CALCPIEC, first routine, 0x4c8c..0x4d1a. Leaves square contents and
 * unused piece-list square bytes untouched, just like the original. */
/* Original: CALCPIEC, file offset 0x4c8c.
 * Purpose: Clear piece-list membership without changing board contents. */
void clear_piece_lists(Position *position) {
    for (int square = 0; square <= 0x77; ++square)
        position->board[square].list_index = 16;
    for (int side = 0; side < 2; ++side) {
        for (int i = 0; i < 16; ++i)
            position->pieces[side][i].piece = 0;
        position->last_piece[side] = position->last_nonpawn[side] = -1;
    }
}

/* CALCPIEC, second routine, 0x4d24..0x4ea8. Preserve its unusual scan
 * order: 00,77,10,67,...,70,07,01,76,... . Piece codes sort ascending;
 * code 6 (pawn) is last. Do not replace this with a rank-major scan:
 * list indices influence later candidate ordering.
 * Preconditions: pieces 0..6, occupied sides 0..1, <=16 pieces per side.
 * Added assertions stop malformed input that would corrupt original memory. */
/* Original: CALCPIEC, file offset 0x4d24.
 * Purpose: Rebuild piece lists in original piece and square order. */
void calculate_piece_lists(Position *position) {
    clear_piece_lists(position);
    for (unsigned piece = 1; piece <= 6; ++piece) {
        if (piece == 6)
            memcpy(position->last_nonpawn, position->last_piece, sizeof position->last_piece);
        unsigned square = 0;
        do {
            Square *cell = &position->board[square];
            if (cell->piece == piece) {
                assert(cell->side < 2);
                int index = ++position->last_piece[cell->side];
                assert(index < 16);
                position->pieces[cell->side][index] = (PieceEntry){square, piece};
                cell->list_index = (int16_t)index;
            }
            square ^= 0x77;
            if (!(square & 4))
                square = square >= 0x70 ? (square + 0x11) & 0x73 : square + 0x10;
        } while (square != 0);
    }
}

/* FILLSAVE, 0x6c8c..0x6d46: 33-byte position snapshot, NOT whole save file.
 * Requires piece <=7 and side <=1; carries the side bit even on empties. */
/* Original: FILLSAVE, file offset 0x6c8c.
 * Purpose: Pack side and board into the original 33-byte snapshot. */
void fill_save(const Position *position, uint8_t out[33]) {
    *out++ = position->side;
    for (unsigned rank = 0; rank < 0x80; rank += 16)
        for (unsigned file = 0; file < 8; file += 2) {
            const Square *a = &position->board[rank + file], *b = a + 1;
            *out++ = (uint8_t)(((a->piece | (a->side << 3)) << 4) | b->piece | (b->side << 3));
        }
}

/* ONLY the board-decoding portion of EXPANDSA, 0x6d9a..0x6e60.
 * INSERTPI at 0x57dc simply writes piece and side, retaining list_index.
 * Excludes original pre-clear call, debug output, ply/state reset at 0x57c8,
 * list rebuild, optional UI updates, and player-control flags after 0x6e74.
 * Call calculate_piece_lists separately after decoding a valid position. */
/* Original: EXPANDSA, file offset 0x6d50; board decoding starts at 0x6d9a.
 * Purpose: Decode only the packed board portion of a snapshot. */
void expand_save_board(Position *position, const uint8_t in[33]) {
    position->side = *in++;
    position->opponent = position->side ^ 1;
    for (unsigned rank = 0; rank < 0x80; rank += 16)
        for (unsigned file = 0; file < 8; file += 2) {
            unsigned byte = *in++;
            position->board[rank + file].piece = (byte >> 4) & 7;
            position->board[rank + file].side = byte >> 7;
            position->board[rank + file + 1].piece = byte & 7;
            position->board[rank + file + 1].side = (byte >> 3) & 1;
        }
}

/* SETUPMSQ, 0xfc2e..0xfd78. Map read from expanded DATA at A5-0x3c02.
 * Display rows reverse engine ranks; each byte packs visible piece (bits 0..2),
 * rendering variant (bits 3..5), and side==0 (bit 6). Variant semantics beyond
 * these exact selection rules are not inferred. */
/* Original: SETUPMSQ, file offset 0xfc2e.
 * Purpose: Map engine pieces to display types and orientations. */
void setup_display_board(const Position *position, uint8_t display[64]) {
    static const uint8_t map[7] = {0, 2, 5, 6, 1, 3, 4};
    for (unsigned i = 0; i < 64; ++i) {
        unsigned square = (((i * 2) & 0x70) ^ 0x70) | (i & 7);
        const Square *cell = &position->board[square];
        assert(cell->piece < sizeof map);
        unsigned piece = map[cell->piece], variant;
        if (piece == 6)
            variant = (i & 7) < 4 ? 3 : 7;
        else if (piece == 1)
            variant = (i & 7) < 4 ? (cell->side == 0 ? 1 : 3) : (cell->side == 0 ? 7 : 5);
        else
            variant = cell->side == 0 ? 0 : 4;
        display[i] = piece ? ((cell->side == 0) << 6) | (variant << 3) | piece : 0;
    }
}

/* AVAIL2MO, 0xfd82..0xfe24: one occupancy bitmask per display row.
 * Original argument treats every nonzero value as the second side. */
/* Original: AVAIL2MO, file offset 0xfd82.
 * Purpose: Build display-row occupancy masks for one side. */
void available_to_move(const uint8_t display[64], int side, uint8_t rows[8]) {
    for (unsigned row = 0; row < 8; ++row) {
        rows[row] = 0;
        for (unsigned col = 0; col < 8; ++col) {
            unsigned cell = display[row * 8 + col];
            if ((cell & 7) && ((cell & 64) ? side == 0 : side != 0))
                rows[row] |= 1u << col;
        }
    }
}

/* SUCCMSG, 0xfe2e..0xfe52. Offset form avoids native pointer size issues.
 * Original adds 20, wraps ONLY on equality with A5-0x5130. */
/* Original: SUCCMSG, file offset 0xfe2e.
 * Purpose: Advance one 20-byte ring slot with the original equality wrap. */
size_t successor_message(size_t offset) {
    offset += 20;
    return offset == 160 ? 0 : offset;
}

/* MOUSEMOV, 0x48f2..0x4922: a ring slot is 20 big-endian bytes.
 * Writes only type=5 and words at +8,+10; other bytes remain unchanged.
 * 0x42d6 supplies these words from table[row*8+column] and row_table[row].
 * These are screen-coordinate fields, not engine square indices. */
/* Original: MOUSEMOV, file offset 0x48f2.
 * Purpose: Write mouse coordinates into the current message slot. */
void mouse_move_message(uint8_t ring[160], size_t *offset, uint16_t x, uint16_t y) {
    assert(*offset < 160 && *offset % 20 == 0);
    uint8_t *slot = ring + *offset;
    slot[0] = slot[1] = slot[2] = 0;
    slot[3] = 5;
    slot[8] = x >> 8;
    slot[9] = x;
    slot[10] = y >> 8;
    slot[11] = y;
    *offset = successor_message(*offset);
}

#ifdef BC_SELF_TEST
#include <stdio.h>
/* Native helper (no direct original address).
 * Purpose: Check recovered board packing, coordinate mapping, lists, and message slots. */
int main(void) {
    Position position = {0}, decoded = {0};
    uint8_t snapshot[33], roundtrip[33], display[64], rows[8], ring[160];
    for (int rank = 0; rank < 8; ++rank)
        for (int file = 0; file < 8; ++file)
            assert(calc_square('A' + file, '1' + rank) == rank * 16 + file);
    assert(calc_square('i', '1') == -1 && calc_square('a', '9') == -1);
    position.side = 1;
    position.board[0] = (Square){1, 0, 0};
    position.board[0x77] = (Square){1, 1, 0};
    position.board[0x10] = (Square){6, 0, 0};
    position.board[0x67] = (Square){6, 1, 0};
    calculate_piece_lists(&position);
    assert(position.last_nonpawn[0] == 0 && position.last_piece[0] == 1);
    assert(position.pieces[0][1].square == 0x10 && position.pieces[1][1].square == 0x67);
    assert(position.board[1].list_index == 16);
    fill_save(&position, snapshot);
    assert(snapshot[0] == 1 && snapshot[1] == 0x10 && snapshot[32] == 9);
    expand_save_board(&decoded, snapshot);
    assert(decoded.side == 1 && decoded.opponent == 0);
    fill_save(&decoded, roundtrip);
    assert(memcmp(snapshot, roundtrip, 33) == 0);
    setup_display_board(&position, display);
    assert(display[56] == 66 && display[7] == 34);
    available_to_move(display, 0, rows);
    assert(rows[7] == 1 && rows[6] == 1 && rows[0] == 0);
    available_to_move(display, 1, rows);
    assert(rows[0] == 128 && rows[1] == 128 && rows[7] == 0);
    memset(ring, 0xa5, sizeof ring);
    size_t offset = 140;
    mouse_move_message(ring, &offset, 0x1234, 0xabcd);
    assert(offset == 0 && ring[143] == 5 && ring[148] == 0x12);
    assert(ring[149] == 0x34 && ring[150] == 0xab && ring[151] == 0xcd);
    assert(ring[144] == 0xa5 && successor_message(0) == 20);
    /* Every possible packed nibble, including empty squares with side=1. */
    for (unsigned byte = 0; byte < 256; ++byte) {
        memset(snapshot + 1, byte, 32);
        expand_save_board(&decoded, snapshot);
        fill_save(&decoded, roundtrip);
        assert(memcmp(snapshot, roundtrip, 33) == 0);
    }
    /* Exercise every square in the original piece-list traversal. */
    for (unsigned square = 0; square < 120; ++square) {
        if (square & 0x88)
            continue;
        memset(&position, 0, sizeof position);
        position.board[square].piece = 2;
        calculate_piece_lists(&position);
        assert(position.last_piece[0] == 0 && position.pieces[0][0].square == square);
    }
    puts("readable_core: all self-checks passed");
    return 0;
}
#endif
