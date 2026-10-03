#ifndef BC_MODEM_HOST_H
#define BC_MODEM_HOST_H
#include "modem_session.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <iterator>
#include <optional>
#include <string>
#include <unistd.h>
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
    explicit ModemHost(const std::string &path) {
        session.fd = path.empty() ? -1 : bc_modem_open(path.c_str());
        session.cancelled = [](void *context) {
            return static_cast<int>(static_cast<ModemHost *>(context)->stop.load());
        };
        session.cancel_context = this;
    }
    /* Native lifetime boundary: interrupt polling, join, then close the device. */
    ~ModemHost() {
        stop = true;
        if (worker.valid())
            worker.wait();
        if (session.fd >= 0)
            close(session.fd);
    }
    /* Native device availability; no original function. */
    bool connected() const {
        return session.fd >= 0;
    }
    /* Native busy guard: pending network mutations must finish before another move. */
    bool busy() const {
        return worker.valid() || !outgoing.empty();
    }
    /* Native adapter for SENDMOVE/SENDBOAR/SENDPROM using the original codec. */
    void packet(uint8_t type, const uint8_t *payload, size_t size) {
        Job job{};
        job.operation = Operation::send_packet;
        job.size = bc_modem_encode(job.bytes.data(), type, payload, size);
        if (job.size)
            outgoing.push_back(job);
    }
    /* Native adapter for SENDGOTI 0x113b6; clears receive-error after transmission. */
    void acknowledge() {
        Job job{};
        job.operation = Operation::acknowledge;
        outgoing.push_back(job);
    }
    /* Native ASCII adapter for modem commands and SENDMESS 0x3bce. */
    void text(const std::string &text) {
        Job job{};
        if (text.size() > job.bytes.size())
            return;
        std::copy(text.begin(), text.end(), job.bytes.begin());
        job.size = text.size();
        outgoing.push_back(job);
    }
    /* HANDLEMO 0x10866: preserve the escape/three-second-delay/hangup ordering. */
    void hang_up() {
        Job job{};
        job.operation = Operation::hang_up;
        outgoing.push_back(job);
    }
    /* Native completion adapter; ended mirrors READBLOC's AA flag0xfcdcc. */
    bool ended() const {
        return !worker.valid() && session.ended;
    }
    /* WAITTOEN 0x3b50 clears the received-end flag after the wait completes. */
    void clear_ended() {
        if (!worker.valid())
            session.ended = 0;
    }
    /* Quit 0x2cd4 -> 0x3cbe: interrupt native I/O, discard pending game sends,
     * then emit the original 24-byte quit line before closing the descriptor. */
    void quit() {
        closing = true;
        stop = true;
        outgoing.clear();
    }
    /* Native close completion; only the SDL thread reads these job flags. */
    bool quit_complete() const {
        return quit_sent && !worker.valid();
    }
    /* Native pump: one finite operation at a time, no SDL access from its worker. */
    std::optional<Result> update(bool receive) {
        if (worker.valid()) {
            if (worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return {};
            return worker.get();
        }
        if (!connected())
            return {};
        Job job{};
        if (closing && !quit_sent) {
            stop = false;
            static const char goodbye[] = "Your Opponent just quit\r";
            std::copy(std::begin(goodbye), std::end(goodbye) - 1, job.bytes.begin());
            job.size = sizeof goodbye - 1;
            quit_sent = true;
        } else if (closing)
            return {};
        else if (!outgoing.empty()) {
            job = outgoing.front();
            outgoing.pop_front();
        } else if (receive && bc_modem_available(&session))
            job.operation = Operation::receive_packet;
        else
            return {};
        worker = std::async(std::launch::async, [this, job] { return execute(job); });
        return {};
    }

  private:
    enum class Operation { send_text, send_packet, acknowledge, hang_up, receive_packet };
    struct Job {
        Operation operation = Operation::send_text;
        size_t size = 0;
        std::array<uint8_t, 256> bytes{};
    };
    /* Native worker adapter for SENDBLOC/READBLOC 0x10ede/0x11018 and
     * SENDGOTI 0x113b6. Keep session mutations on one worker; return owned
     * packet/message values for the SDL thread to consume after completion. */
    Result execute(const Job &job) {
        Result result;
        result.bytes = job.bytes;
        session.context = &result.messages;
        session.message = [](void *context, const char *text) {
            static_cast<std::vector<std::string> *>(context)->emplace_back(text);
        };
        switch (job.operation) {
        case Operation::send_text:
            result.status = bc_modem_write(&session, job.bytes.data(), job.size);
            break;
        case Operation::send_packet:
            result.status = bc_modem_send_block(&session, result.bytes.data());
            break;
        case Operation::acknowledge:
            result.status = bc_modem_acknowledge(&session);
            break;
        case Operation::hang_up:
            result.status = bc_modem_hang_up(&session);
            break;
        case Operation::receive_packet:
            result.received = true;
            result.status = bc_modem_read_block(&session, result.bytes.data());
            break;
        }
        return result;
    }
    BCModemSession session{};
    std::atomic<bool> stop{false};
    std::future<Result> worker;
    std::deque<Job> outgoing;
    bool closing = false, quit_sent = false;
};
#endif
