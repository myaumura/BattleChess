#ifndef BC_MODEM_PROTOCOL_H
#define BC_MODEM_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { BC_MODEM_FRAME_CAPACITY = 39 };
/* Output is type,length,payload,checksum-even,checksum-odd. Returns 0 on bad input. */
size_t bc_modem_encode(uint8_t out[BC_MODEM_FRAME_CAPACITY], uint8_t type, const uint8_t *payload,
                       size_t payload_size);
/* Complete binary frame only; text and serial retry policy belong to the host. */
int bc_modem_frame_valid(const uint8_t *frame, size_t size);
#ifdef __cplusplus
}
#endif
#endif
