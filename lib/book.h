#ifndef BC_BOOK_H
#define BC_BOOK_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* ALLCANM1.data offset 0x24fad, length 32000. History entries MUST be
 * zero-based ordinals in the original INITMOVG/sub_0000d372 order.
 * Returns 1 and an ordinal, or 0 for exhausted/invalid book. No search fallback.
 * RNG state is the original uint32 LCG state, advanced only on success. */
int book_move(const uint8_t *book, size_t length, const uint8_t *history, size_t plies,
              uint32_t *random_state, uint8_t *ordinal);
#ifdef __cplusplus
}
#endif
#endif
