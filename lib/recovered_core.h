/* 
 * Native interface to the hand-translated Macintosh routines.
 * Original offsets and deliberate changes are documented in recovered_core.c.
 */

#ifndef BC_RECOVERED_CORE_H
#define BC_RECOVERED_CORE_H

#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t piece;
    uint8_t side;
    int16_t list_index;
} Square;

typedef struct {
    uint8_t square;
    uint8_t piece;
} PieceEntry;

typedef struct {
    Square board[120];        /* A5-0x6c54, 4 bytes per 0x88 square */
    PieceEntry pieces[2][16]; /* A5-0x6a74 */
    int16_t last_nonpawn[2];  /* A5-0x6a34: last index, NOT count */
    int16_t last_piece[2];    /* A5-0x6a30: last index, NOT count */
    uint8_t side, opponent;   /* A5-0x6a2c and A5-0x6a2b */
} Position;

int calc_square(unsigned char file, unsigned char rank);
void clear_piece_lists(Position *p);
void calculate_piece_lists(Position *p);
void fill_save(const Position *p, uint8_t out[33]);
void expand_save_board(Position *p, const uint8_t in[33]);
void setup_display_board(const Position *p, uint8_t display[64]);
void available_to_move(const uint8_t display[64], int side, uint8_t rows[8]);
size_t successor_message(size_t offset);
void mouse_move_message(uint8_t ring[160], size_t *offset, uint16_t x, uint16_t y);
/* Additional translations; see board.c for assembly provenance. */
int insert_piece(Position *p, unsigned piece, unsigned side, unsigned square);
void reset_board(Position *p);

#ifdef __cplusplus
}
#endif

#endif
