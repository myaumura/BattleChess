#define _POSIX_C_SOURCE 200809L
#include "save_game.h"
#include "setup_board.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#ifdef __APPLE__
#include <sys/xattr.h>
#endif

/* SAVEREQ 0x8d34; DOLOAD 0x90ca and DOSAVE 0x91da transfer 0x4e
 * bytes at A5-0x14d8. No signature, history, or endian-sized fields added. */
/* Original: SAVEREQ, file offset 0x8d34.
 * Purpose: Update known board/settings fields while preserving opaque save bytes. */
int bc_save_encode(BCSaveFile *file, const Position *position, const BCSaveSettings *s) {
    if (!file || !s || bc_setup_validate(position) || s->white_player > 3 || s->black_player > 3 ||
        s->board_2d > 1 || s->sound > 1)
        return 0;
    fill_save(position, file->bytes + 40);
    file->bytes[74] = s->level;
    /* PUTSETTI explicitly clears flag_20 before storing. */
    file->bytes[75] =
        s->black_player | (s->white_player << 2) | (s->board_2d << 4) | (s->sound << 6);
    return 1;
}

/* Native helper (no direct original address).
 * Purpose: Validate and decode the DOLOAD (0x90ca), EXPANDSA (0x6d50), and GETSETTI (0x74b4)
 * fields. */
int bc_save_decode(Position *position, BCSaveSettings *s, const uint8_t *bytes, size_t size) {
    if (!position || !s || !bytes || size != BC_SAVE_FILE_SIZE)
        return 0;
    Position candidate = {0};
    expand_save_board(&candidate, bytes + 40);
    if (bc_setup_validate(&candidate))
        return 0;
    calculate_piece_lists(&candidate);
    BCSaveSettings settings = {bytes[74],
                               (bytes[75] >> 2) & 3,
                               bytes[75] & 3,
                               (bytes[75] >> 4) & 1,
                               (bytes[75] >> 6) & 1,
                               (bytes[75] >> 5) & 1};
    *position = candidate;
    *s = settings;
    return 1;
}

/* Native helper (no direct original address).
 * Purpose: Read exactly one original 78-byte save and commit only validated output. */
int bc_save_read(const char *path, BCSaveFile *file, Position *position, BCSaveSettings *s) {
    if (!path || !file)
        return 0;
    FILE *input = fopen(path, "rb");
    if (!input)
        return 0;
    BCSaveFile candidate;
    size_t n = fread(candidate.bytes, 1, sizeof candidate.bytes, input);
    int extra = fgetc(input), failed = ferror(input);
    int closed = fclose(input);
    if (n != sizeof candidate.bytes || extra != EOF || failed || closed ||
        !bc_save_decode(position, s, candidate.bytes, n))
        return 0;
    *file = candidate;
    return 1;
}

/* Native helper (no direct original address).
 * Purpose: Write a temporary data fork and rename after successful close to preserve existing
 * saves. On macOS retain DOSAVE's 'Game'/'IPBC' identity (0x929c..0x92a8),
 * required by DOLOAD's file-type filter at 0x9100. */
int bc_save_write(const char *path, const BCSaveFile *file) {
    if (!path || !file)
        return 0;
    size_t length = strlen(path);
    char *temporary = malloc(length + sizeof ".XXXXXX");
    if (!temporary)
        return 0;
    memcpy(temporary, path, length);
    memcpy(temporary + length, ".XXXXXX", sizeof ".XXXXXX");
    int fd = mkstemp(temporary);
    FILE *output = fd < 0 ? NULL : fdopen(fd, "wb");
    int ok = 0;
    if (output) {
        int metadata_ok = 1;
#ifdef __APPLE__
        const uint8_t finder_info[32] = {'G', 'a', 'm', 'e', 'I', 'P', 'B', 'C'};
        metadata_ok =
            fsetxattr(fd, "com.apple.FinderInfo", finder_info, sizeof finder_info, 0, 0) == 0;
#endif
        size_t n = metadata_ok ? fwrite(file->bytes, 1, sizeof file->bytes, output) : 0;
        int closed = fclose(output);
        ok = n == sizeof file->bytes && closed == 0 && rename(temporary, path) == 0;
    } else if (fd >= 0)
        close(fd);
    if (!ok)
        unlink(temporary);
    free(temporary);
    return ok;
}
