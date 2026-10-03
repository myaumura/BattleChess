#define _POSIX_C_SOURCE 200809L
#include "modem_session.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* INITSERI 0x113e0, config0x4d7c: native POSIX replacement for modem-port
 * drivers, 300 baud, eight data bits, no parity, one stop bit. */
int bc_modem_open(const char *path) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return -1;
    struct termios settings;
    if (tcgetattr(fd, &settings) < 0) {
        close(fd);
        return -1;
    }
    settings.c_iflag = 0;
    settings.c_oflag = 0;
    settings.c_lflag = 0;
    settings.c_cflag = CS8 | CREAD | CLOCAL;
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;
    cfsetispeed(&settings, B300);
    cfsetospeed(&settings, B300);
    if (tcsetattr(fd, TCSANOW, &settings) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* INSERIAL 0x115b8/INPORTNO 0x114cc: native readiness replaces serial count. */
int bc_modem_available(const BCModemSession *session) {
    if (!session || session->fd < 0)
        return 0;
    struct pollfd descriptor = {session->fd, POLLIN, 0};
    int result;
    do {
        result = poll(&descriptor, 1, 0);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & POLLIN);
}

/* SERIALWR 0x1148e: original byte ordering, native short-write/error handling. */
int bc_modem_write(BCModemSession *session, const uint8_t *bytes, size_t size) {
    if (!session || session->fd < 0 || (size && !bytes))
        return -2;
    while (size) {
        if (session->cancelled && session->cancelled(session->cancel_context))
            return -2;
        ssize_t count = write(session->fd, bytes, 1);
        if (count == 1) {
            ++bytes;
            --size;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd descriptor = {session->fd, POLLOUT, 0};
            int ready = poll(&descriptor, 1, 200);
            if (ready > 0 && (descriptor.revents & POLLOUT))
                continue;
            if (ready < 0 && errno == EINTR)
                continue;
            if (!ready)
                continue;
        }
        return -2;
    }
    return 0;
}

/* SERIALRE 0x114f0: per-byte wall-clock deadline +16, native poll adapter. */
static int read_bytes(BCModemSession *session, uint8_t *out, size_t size) {
    while (size--) {
        time_t deadline = time(NULL) + 16;
        for (;;) {
            if (session->cancelled && session->cancelled(session->cancel_context))
                return -2;
            time_t remaining = deadline - time(NULL);
            if (remaining <= 0)
                return -1;
            struct pollfd descriptor = {session->fd, POLLIN, 0};
            int ready = poll(&descriptor, 1, 200);
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready < 0)
                return -2;
            if (!ready)
                continue;
            if (!(descriptor.revents & POLLIN))
                return -2;
            ssize_t count = read(session->fd, out, 1);
            if (count == 1) {
                ++out;
                break;
            }
            if (count < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            return -2;
        }
    }
    return 0;
}

/* SERIALFL 0x11584: drain bytes currently available using the native device. */
static void flush_input(BCModemSession *session) {
    uint8_t ignored;
    while (bc_modem_available(session))
        if (read(session->fd, &ignored, 1) != 1)
            break;
}

/* READBLOC 0x11018: consumes control packets/text, returns ordinary packets.
 * Native guards bound the original unbounded ASCII buffer and short lengths. */
int bc_modem_read_block(BCModemSession *session, uint8_t bytes[256]) {
    if (!session || session->fd < 0 || !bytes)
        return -2;
    for (;;) {
        int result = read_bytes(session, bytes, 1);
        if (result)
            return result;
        if ((bytes[0] & 0xf0) == 0xa0) {
            if ((result = read_bytes(session, bytes + 1, 1)))
                return result;
            size_t size = bytes[1] & 0x7f;
            if (size < 4) {
                flush_input(session);
                return -1;
            }
            if (size >= 40) {
                flush_input(session);
                continue;
            }
            if ((result = read_bytes(session, bytes + 2, size - 2)))
                return result;
            if (!bc_modem_frame_valid(bytes, size)) {
                flush_input(session);
                session->receive_error = 1;
                return -1;
            }
            if (bytes[0] == 0xaa)
                session->ended = 1;
            else if (bytes[0] == 0xa6) {
                uint8_t reply[BC_MODEM_FRAME_CAPACITY];
                size_t length = bc_modem_encode(reply, session->receive_error ? 0xa4 : 0xa5, 0, 0);
                if ((result = bc_modem_write(session, reply, length)))
                    return result;
            } else
                return (int)size;
        } else if (bytes[0] < 0x80) {
            if (bytes[0] != '\n') {
                size_t length = 0;
                while (bytes[length] != '\r') {
                    if (bytes[length] >= 0x20) {
                        if (length == 255) {
                            flush_input(session);
                            return -1;
                        }
                        ++length;
                    }
                    result = read_bytes(session, bytes + length, 1);
                    if (result == -2)
                        return result;
                    if (result == -1)
                        bytes[length] = '\r';
                }
                bytes[length] = 0;
                if (length && strcmp((const char *)bytes, "ATE") && session->message)
                    session->message(session->context, (const char *)bytes);
            }
        } else {
            flush_input(session);
            session->receive_error = 1;
            return -1;
        }
        if (!bc_modem_available(session))
            return -1;
    }
}

/* SENDBLOC 0x10ede: preserves shared caller buffer and original five-response
 * counter (even a fifth-response ACK reaches the original failure alert). */
int bc_modem_send_block(BCModemSession *session, uint8_t bytes[256]) {
    if (!session || !bytes || !bc_modem_frame_valid(bytes, bytes[1] & 0x7f))
        return -2;
    size_t body_size = (bytes[1] - 2) & 0x7f;
    uint8_t sums[2] = {bytes[body_size], bytes[body_size + 1]};
    int result = bc_modem_write(session, bytes, body_size);
    if (result || (result = bc_modem_write(session, sums, 2)))
        return result;
    for (int count = 1; count <= 5; ++count) {
        result = bc_modem_read_block(session, bytes);
        if (result == -2)
            return result;
        if (result == -1) {
            static const uint8_t query[] = {0xa6, 4, 0xc6, 0x34};
            if ((result = bc_modem_write(session, query, 4)))
                return result;
            bytes[0] = 0;
        } else if (bytes[0] == 0xa4) {
            if ((result = bc_modem_write(session, bytes, body_size)))
                return result;
            if ((result = bc_modem_write(session, sums, 2)))
                return result;
        } else if (bytes[0] != 0xa5)
            return -1;
        if (bytes[0] == 0xa5)
            return count < 5 ? 0 : -1;
    }
    return -1;
}

/* SENDGOTI 0x113b6: send literal ACK and clear original receive-error flag. */
int bc_modem_acknowledge(BCModemSession *session) {
    static const uint8_t ack[] = {0xa5, 4, 0xc5, 0x34};
    int result = bc_modem_write(session, ack, 4);
    if (!result)
        session->receive_error = 0;
    return result;
}

/* DIALNUMB 0x39ca..0x3aae: native caller supplies dialog text; exact odd cap. */
int bc_modem_dial(BCModemSession *session, const char *number) {
    if (!number)
        return -2;
    size_t length = strlen(number);
    if (!length)
        return 0;
    if (length > 40)
        length = 38;
    uint8_t command[46];
    memcpy(command, "ATDT ", 5);
    memcpy(command + 5, number, length);
    command[5 + length] = '\r';
    return bc_modem_write(session, command, length + 6);
}

/* HANDLEMO 0x10866: +++, Delay(180 ticks), ATH CR; native nanosleep. */
int bc_modem_hang_up(BCModemSession *session) {
    int result = bc_modem_write(session, (const uint8_t *)"+++", 3);
    if (result)
        return result;
    /* Native cancellation boundary: preserve at least three seconds normally,
     * but let quit interrupt the guard before its own bounded send deadline. */
    for (int step = 0; step < 30; ++step) {
        if (session->cancelled && session->cancelled(session->cancel_context))
            return -2;
        struct timespec delay = {0, 100000000};
        while (nanosleep(&delay, &delay)) {
            if (errno != EINTR)
                return -2;
            if (session->cancelled && session->cancelled(session->cancel_context))
                return -2;
        }
    }
    return bc_modem_write(session, (const uint8_t *)"ATH\r", 4);
}
