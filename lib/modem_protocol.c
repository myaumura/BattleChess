#include "modem_protocol.h"
#include <string.h>

/* SENDBLOC 0x10ee2..0x10f38: alternating additive byte checksums; native buffers. */
static void checksum(const uint8_t *bytes, size_t size, uint8_t result[2]) {
    result[0] = 0x20;
    result[1] = 0x30;
    for (size_t i = 0; i < size; ++i)
        result[i & 1] = (uint8_t)(result[i & 1] + bytes[i]);
}

/* SENDBLOC 0x10ede: original wire format, with native capacity/input guards. */
size_t bc_modem_encode(uint8_t out[BC_MODEM_FRAME_CAPACITY], uint8_t type, const uint8_t *payload,
                       size_t payload_size) {
    if (!out || (type & 0xf0) != 0xa0 || payload_size > BC_MODEM_FRAME_CAPACITY - 4 ||
        (payload_size && !payload))
        return 0;
    /* Permit an in-place payload while installing its two-byte header. */
    if (payload_size)
        memmove(out + 2, payload, payload_size);
    out[0] = type;
    out[1] = (uint8_t)(payload_size + 4);
    checksum(out, payload_size + 2, out + payload_size + 2);
    return payload_size + 4;
}

/* READBLOC 0x11018..0x11104: binary header/checksum validation; native bounds
 * reject lengths below 4 instead of reproducing the original underflow. */
int bc_modem_frame_valid(const uint8_t *frame, size_t size) {
    uint8_t expected[2];
    if (!frame || size < 4 || size > BC_MODEM_FRAME_CAPACITY || (frame[0] & 0xf0) != 0xa0 ||
        (frame[1] & 0x7f) != size)
        return 0;
    checksum(frame, size - 2, expected);
    return frame[size - 2] == expected[0] && frame[size - 1] == expected[1];
}
