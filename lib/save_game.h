#ifndef BC_SAVE_GAME_H
#define BC_SAVE_GAME_H
#include "recovered_core.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { BC_SAVE_FILE_SIZE = 78 };
typedef struct {
    uint8_t bytes[BC_SAVE_FILE_SIZE];
} BCSaveFile;
typedef struct {
    uint8_t level, white_player, black_player, board_2d, sound, flag_20;
} BCSaveSettings;
/* Encode updates known fields and preserves opaque bytes. Zero-init new files. */
int bc_save_encode(BCSaveFile *, const Position *, const BCSaveSettings *);
int bc_save_decode(Position *, BCSaveSettings *, const uint8_t *, size_t);
int bc_save_read(const char *, BCSaveFile *, Position *, BCSaveSettings *);
int bc_save_write(const char *, const BCSaveFile *);
#ifdef __cplusplus
}
#endif
#endif
