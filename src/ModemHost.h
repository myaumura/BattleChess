#ifndef BC_MODEM_HOST_H
#define BC_MODEM_HOST_H

#include "modem_session.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <optional>
#include <string>
#include <vector>

/* Native asynchronous transport adapter around original SENDBLOC/READBLOC.
 * One job owns session state; the SDL thread consumes completed values only. */
class ModemHost {
  public:
    struct Result {
        int status = 0;
        bool received = false;
        std::array<uint8_t, 256> bytes{};
        std::vector<std::string> messages;
    };

    /* Native ownership boundary, replacing fixed modem-port driver references. */
    explicit ModemHost(const std::string &path);
    /* Native lifetime boundary: interrupt polling, join, then close the device. */
    ~ModemHost();

    /* Native device availability; no original function. */
    bool connected() const;
    /* Native busy guard: pending network mutations must finish before another move. */
    bool busy() const;

    /* Native adapter for SENDMOVE/SENDBOAR/SENDPROM using the original codec. */
    void packet(uint8_t type, const uint8_t *payload, size_t size);
    /* Native adapter for SENDGOTI 0x113b6; clears receive-error after transmission. */
    void acknowledge();
    /* Native ASCII adapter for modem commands and SENDMESS 0x3bce. */
    void text(const std::string &text);
    /* HANDLEMO 0x10866: preserve the escape/three-second-delay/hangup ordering. */
    void hangUp();

    /* Native completion adapter; ended mirrors READBLOC's AA flag0xfcdcc. */
    bool ended() const;
    /* WAITTOEN 0x3b50 clears the received-end flag after the wait completes. */
    void clearEnded();
    /* Quit 0x2cd4 -> 0x3cbe: interrupt native I/O, discard pending game sends,
     * then emit the original 24-byte quit line before closing the descriptor. */
    void quit();
    /* Native close completion; only the SDL thread reads these job flags. */
    bool quitComplete() const;

    /* Native pump: one finite operation at a time, no SDL access from its worker. */
    std::optional<Result> update(bool receive);

  private:
    enum class Operation {
        sendText,
        sendPacket,
        acknowledge,
        hangUp,
        receivePacket,
    };

    struct Job {
        Operation operation = Operation::sendText;
        size_t size = 0;
        std::array<uint8_t, 256> bytes{};
    };

    std::optional<Job> takeNextJob(bool receive);
    Result execute(const Job &job);

    BCModemSession session{};
    std::atomic<bool> stopRequested{false};
    std::future<Result> worker;
    std::deque<Job> outgoingJobs;
    bool closing = false, quitSent = false;
};

#endif
