#define _POSIX_C_SOURCE 200809L
#include "save_game.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/xattr.h>
#endif
// Native validation (no original entrypoint): Run the save game check regression assertions.
int main(void) {
    Position p = {0}, loaded = {0};
    reset_board(&p);
    BCSaveFile file;
    memset(&file, 0xa5, sizeof file);
    BCSaveSettings settings = {3, 1, 2, 1, 1, 1}, decoded;
    assert(bc_save_encode(&file, &p, &settings));
    /* Independent original nibble layout: R,N,B,Q,K,B,N,R. */
    static const uint8_t back[] = {0x35, 0x42, 0x14, 0x53};
    assert(file.bytes[40] == 0);
    assert(memcmp(file.bytes + 41, back, 4) == 0);
    for (int i = 45; i < 49; ++i)
        assert(file.bytes[i] == 0x66);
    for (int i = 49; i < 65; ++i)
        assert(file.bytes[i] == 0);
    for (int i = 65; i < 69; ++i)
        assert(file.bytes[i] == 0xee);
    for (int i = 0; i < 4; ++i)
        assert(file.bytes[69 + i] == (back[i] | 0x88));
    assert(file.bytes[74] == 3 && file.bytes[75] == 0x56);
    assert(file.bytes[0] == 0xa5 && file.bytes[39] == 0xa5 && file.bytes[73] == 0xa5 &&
           file.bytes[76] == 0xa5 && file.bytes[77] == 0xa5);
    assert(bc_save_decode(&loaded, &decoded, file.bytes, 78));
    assert(decoded.flag_20 == 0 && decoded.white_player == 1 && decoded.black_player == 2);
    for (int i = 0; i < 120; ++i)
        if (!(i & 0x88)) {
            assert(p.board[i].piece == loaded.board[i].piece);
            assert(p.board[i].side == loaded.board[i].side);
        }
    char path[] = "/tmp/bc-save-check-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    assert(bc_save_write(path, &file));
#ifdef __APPLE__
    /* DOLOAD filters for 'Game' at 0x9100; DOSAVE creates 'Game'/'IPBC'. */
    uint8_t finder_info[32];
    assert(getxattr(path, "com.apple.FinderInfo", finder_info, sizeof finder_info, 0, 0) == 32);
    assert(memcmp(finder_info, "GameIPBC", 8) == 0);
#endif
    BCSaveFile disk;
    assert(bc_save_read(path, &disk, &loaded, &decoded));
    assert(memcmp(&file, &disk, sizeof file) == 0);
    FILE *extra = fopen(path, "ab");
    assert(extra);
    assert(fputc(0, extra) == 0);
    assert(fclose(extra) == 0);
    assert(!bc_save_read(path, &disk, &loaded, &decoded));
    assert(unlink(path) == 0);
    Position before = loaded;
    assert(!bc_save_decode(&loaded, &decoded, file.bytes, 77));
    file.bytes[41] = 0x77;
    assert(!bc_save_decode(&loaded, &decoded, file.bytes, 78));
    assert(memcmp(&before, &loaded, sizeof loaded) == 0);
    return 0;
}
