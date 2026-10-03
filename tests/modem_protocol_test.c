#include "../lib/modem_protocol.h"
#include <assert.h>
#include <string.h>

/* Native regression check against literal packets in SENDGOTI 0x113b6 and
 * SENDBLOC 0x10f6a; SENDMOVE 0x11316 and SENDPROM 0x11370 field order. */
int main(void) {
    uint8_t frame[BC_MODEM_FRAME_CAPACITY];
    const uint8_t ack[] = {0xa5, 4, 0xc5, 0x34};
    const uint8_t query[] = {0xa6, 4, 0xc6, 0x34};
    const uint8_t move[] = {0xa2, 6, 0x34, 0x14, 0xf6, 0x4a};
    const uint8_t promote[] = {0xa9, 5, 2, 0xcb, 0x35};
    assert(bc_modem_encode(frame, 0xa5, 0, 0) == 4);
    assert(memcmp(frame, ack, 4) == 0 && bc_modem_frame_valid(frame, 4));
    assert(bc_modem_frame_valid(query, sizeof query));
    assert(bc_modem_encode(frame, 0xa2, move + 2, 2) == sizeof move);
    assert(memcmp(frame, move, sizeof move) == 0);
    assert(bc_modem_encode(frame, 0xa9, promote + 2, 1) == sizeof promote);
    assert(memcmp(frame, promote, sizeof promote) == 0);
    for (size_t i = 0; i < sizeof move; ++i) {
        memcpy(frame, move, sizeof move);
        frame[i] ^= 1;
        assert(!bc_modem_frame_valid(frame, sizeof move));
    }
    assert(!bc_modem_frame_valid(move, 3));
    assert(!bc_modem_frame_valid(0, 6));
    assert(!bc_modem_encode(frame, 0xa2, 0, 2));
    assert(!bc_modem_encode(frame, 0xa2, move, 36));
    assert(!bc_modem_encode(frame, 0x92, move, 2));
    memset(frame, 0xff, 35);
    assert(bc_modem_encode(frame, 0xa1, frame, 35) == 39);
    assert(bc_modem_frame_valid(frame, 39));
    return 0;
}
