#ifndef BC_MODEM_SESSION_H
#define BC_MODEM_SESSION_H
#include "modem_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int fd;
    int receive_error;
    int ended;
    void (*message)(void *context, const char *text);
    void *context;
    int (*cancelled)(void *context);
    void *cancel_context;
} BCModemSession;
/* Native device adapter. Caller owns returned descriptor and closes it. */
int bc_modem_open(const char *path);
int bc_modem_available(const BCModemSession *session);
int bc_modem_write(BCModemSession *session, const uint8_t *bytes, size_t size);
/* READBLOC writes into a 256-byte caller buffer, including on failure.
 * Returns frame size, -1 timeout/protocol failure, -2 native I/O failure. */
int bc_modem_read_block(BCModemSession *session, uint8_t bytes[256]);
/* SENDBLOC intentionally shares its send/receive buffer, including original
 * retry behavior. 0=ACK, -1=original failure alert, -2=native I/O failure. */
int bc_modem_send_block(BCModemSession *session, uint8_t bytes[256]);
int bc_modem_acknowledge(BCModemSession *session);
int bc_modem_dial(BCModemSession *session, const char *number);
int bc_modem_hang_up(BCModemSession *session);
#ifdef __cplusplus
}
#endif
#endif
