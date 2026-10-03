#define _XOPEN_SOURCE 600
#include "../lib/modem_session.h"
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>

/* Native PTY peer: fixed three-second guard makes broken protocol tests fail. */
static void read_exact(int fd, uint8_t *bytes, size_t size) {
    while (size--) {
        struct pollfd descriptor = {fd, POLLIN, 0};
        assert(poll(&descriptor, 1, 3000) == 1);
        assert(read(fd, bytes++, 1) == 1);
    }
}

/* READBLOC 0x1117a text path: native notification observer. */
static void message(void *context, const char *text) {
    assert(strcmp(text, "CONNECT") == 0);
    ++*(int *)context;
}

/* Native hangup cancellation regression: request cancellation after one guard slice. */
static int cancel_guard(void *context) {
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    struct timespec *start = context;
    return (now.tv_sec - start->tv_sec) * 1000000000LL + now.tv_nsec - start->tv_nsec >= 100000000;
}

/* SENDBLOC 0x10fd6..0x11006: the fifth response fails even when it is ACK.
 * A4 overwrites the caller's buffer before the next six-byte retransmission. */
static void response_limit(int responses, int expected_result) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    int slave = bc_modem_open(ptsname(master));
    assert(slave >= 0);
    struct termios settings;
    assert(tcgetattr(slave, &settings) == 0);
    assert(cfgetispeed(&settings) == B300 && cfgetospeed(&settings) == B300);
    assert((settings.c_cflag & CSIZE) == CS8 && !(settings.c_cflag & (PARENB | CSTOPB)));
    int finished[2];
    assert(pipe(finished) == 0);
    pid_t peer = fork();
    assert(peer >= 0);
    if (!peer) {
        close(slave);
        close(finished[1]);
        uint8_t bytes[6];
        for (int response = 1; response <= responses; ++response) {
            read_exact(master, bytes, sizeof bytes);
            assert(memcmp(bytes,
                          response == 1 ? "\xa2\x06\x34\x14\xf6\x4a" : "\xa4\x04\xc4\x34\xf6\x4a",
                          6) == 0);
            const char *reply = response == responses ? "\xa5\x04\xc5\x34" : "\xa4\x04\xc4\x34";
            assert(write(master, reply, 4) == 4);
        }
        /* Keep the serial peer connected until the final ACK has been consumed. */
        assert(read(finished[0], bytes, 1) == 1);
        close(finished[0]);
        close(master);
        _exit(0);
    }
    close(master);
    close(finished[0]);
    BCModemSession session = {slave, 0, 0, 0, 0, 0, 0};
    uint8_t bytes[256] = {0xa2, 6, 0x34, 0x14, 0xf6, 0x4a};
    int actual_result = bc_modem_send_block(&session, bytes);
    assert(write(finished[1], "x", 1) == 1);
    close(finished[1]);
    assert(actual_result == expected_result);
    int status;
    assert(waitpid(peer, &status, 0) == peer && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(slave);
}

/* Native two-process PTY check of SENDBLOC/READBLOC against literal wire bytes. */
int main(void) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    int slave = bc_modem_open(ptsname(master));
    assert(slave >= 0);
    pid_t peer = fork();
    assert(peer >= 0);
    if (!peer) {
        close(slave);
        uint8_t bytes[256];
        const uint8_t move[] = {0xa2, 6, 0x34, 0x14, 0xf6, 0x4a};
        read_exact(master, bytes, 6);
        assert(memcmp(bytes, move, 6) == 0);
        assert(write(master, "\xa4\x04\xc4\x34", 4) == 4);
        read_exact(master, bytes, 6);
        /* Original reuses overwritten header/data but retains original sums. */
        assert(memcmp(bytes, "\xa4\x04\xc4\x34\xf6\x4a", 6) == 0);
        assert(write(master, "\xa5\x04\xc5\x34", 4) == 4);
        read_exact(master, bytes, 4);
        assert(memcmp(bytes, "ATE\r", 4) == 0);
        assert(write(master, "ATE\rCONNECT\r", 12) == 12);
        assert(write(master, "\xa6\x04\xc6\x34", 4) == 4);
        read_exact(master, bytes, 4);
        assert(memcmp(bytes, "\xa5\x04\xc5\x34", 4) == 0);
        assert(write(master, move, 6) == 6);
        read_exact(master, bytes, 4);
        assert(memcmp(bytes, "\xa5\x04\xc5\x34", 4) == 0);
        read_exact(master, bytes, 9);
        assert(memcmp(bytes, "ATDT 123\r", 9) == 0);
        close(master);
        _exit(0);
    }
    close(master);
    int messages = 0;
    BCModemSession session = {slave, 0, 0, message, &messages, 0, 0};
    uint8_t bytes[256] = {0};
    const uint8_t squares[] = {0x34, 0x14};
    assert(bc_modem_encode(bytes, 0xa2, squares, 2) == 6);
    assert(bc_modem_send_block(&session, bytes) == 0);
    assert(bc_modem_write(&session, (const uint8_t *)"ATE\r", 4) == 0);
    int result;
    do {
        result = bc_modem_read_block(&session, bytes);
    } while (result == -1);
    assert(result == 6 && bytes[0] == 0xa2 && bytes[2] == 0x34 && bytes[3] == 0x14);
    assert(messages == 1);
    assert(bc_modem_acknowledge(&session) == 0);
    assert(bc_modem_dial(&session, "123") == 0);
    int status;
    assert(waitpid(peer, &status, 0) == peer && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(slave);
    master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    slave = bc_modem_open(ptsname(master));
    assert(slave >= 0);
    struct timespec start, end;
    assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    BCModemSession cancelling = {slave, 0, 0, 0, 0, cancel_guard, &start};
    assert(bc_modem_hang_up(&cancelling) == -2);
    assert(clock_gettime(CLOCK_MONOTONIC, &end) == 0);
    assert((end.tv_sec - start.tv_sec) * 1000000000LL + end.tv_nsec - start.tv_nsec < 1000000000);
    read_exact(master, bytes, 3);
    assert(memcmp(bytes, "+++", 3) == 0);
    struct pollfd descriptor = {master, POLLIN, 0};
    assert(poll(&descriptor, 1, 0) == 0); // Cancelled hangup must not emit ATH.
    close(slave);
    close(master);
    response_limit(4, 0);
    response_limit(5, -1);
    return 0;
}
