#include "ModemHost.h"
#include <algorithm>
#include <chrono>
#include <iterator>
#include <unistd.h>

namespace {
    constexpr char kQuitMessage[] = "Your Opponent just quit\r";
} // namespace

ModemHost::ModemHost(const std::string &path) {
    session.fd = path.empty() ? -1 : bc_modem_open(path.c_str());
    session.cancelled = [](void *context) {
        return static_cast<int>(static_cast<ModemHost *>(context)->stopRequested.load());
    };
    session.cancel_context = this;
}

ModemHost::~ModemHost() {
    stopRequested = true;
    if (worker.valid())
        worker.wait();
    if (session.fd >= 0)
        close(session.fd);
}

bool ModemHost::connected() const {
    return session.fd >= 0;
}

bool ModemHost::busy() const {
    return worker.valid() || !outgoingJobs.empty();
}

void ModemHost::packet(uint8_t type, const uint8_t *payload, size_t size) {
    Job job{};
    job.operation = Operation::sendPacket;
    job.size = bc_modem_encode(job.bytes.data(), type, payload, size);
    if (job.size)
        outgoingJobs.push_back(job);
}

void ModemHost::acknowledge() {
    Job job{};
    job.operation = Operation::acknowledge;
    outgoingJobs.push_back(job);
}

void ModemHost::text(const std::string &text) {
    Job job{};
    if (text.size() > job.bytes.size())
        return;
    std::copy(text.begin(), text.end(), job.bytes.begin());
    job.size = text.size();
    outgoingJobs.push_back(job);
}

void ModemHost::hangUp() {
    Job job{};
    job.operation = Operation::hangUp;
    outgoingJobs.push_back(job);
}

bool ModemHost::ended() const {
    return !worker.valid() && session.ended;
}

void ModemHost::clearEnded() {
    if (!worker.valid())
        session.ended = 0;
}

void ModemHost::quit() {
    closing = true;
    stopRequested = true;
    outgoingJobs.clear();
}

bool ModemHost::quitComplete() const {
    return quitSent && !worker.valid();
}

std::optional<ModemHost::Result> ModemHost::update(bool receive) {
    if (worker.valid()) {
        if (worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return {};
        return worker.get();
    }
    if (!connected())
        return {};
    auto nextJob = takeNextJob(receive);
    if (!nextJob)
        return {};
    worker = std::async(std::launch::async, [this, job = *nextJob] { return execute(job); });
    return {};
}

std::optional<ModemHost::Job> ModemHost::takeNextJob(bool receive) {
    Job job{};
    if (closing && !quitSent) {
        stopRequested = false;
        std::copy(std::begin(kQuitMessage), std::end(kQuitMessage) - 1, job.bytes.begin());
        job.size = sizeof kQuitMessage - 1;
        quitSent = true;
    } else if (closing)
        return {};
    else if (!outgoingJobs.empty()) {
        job = outgoingJobs.front();
        outgoingJobs.pop_front();
    } else if (receive && bc_modem_available(&session))
        job.operation = Operation::receivePacket;
    else
        return {};
    return job;
}

/* Native worker adapter for SENDBLOC/READBLOC 0x10ede/0x11018 and
 * SENDGOTI 0x113b6. Keep session mutations on one worker; return owned
 * packet/message values for the SDL thread to consume after completion. */
ModemHost::Result ModemHost::execute(const Job &job) {
    Result result;
    result.bytes = job.bytes;
    session.context = &result.messages;
    session.message = [](void *context, const char *text) {
        static_cast<std::vector<std::string> *>(context)->emplace_back(text);
    };
    switch (job.operation) {
    case Operation::sendText:
        result.status = bc_modem_write(&session, job.bytes.data(), job.size);
        break;
    case Operation::sendPacket:
        result.status = bc_modem_send_block(&session, result.bytes.data());
        break;
    case Operation::acknowledge:
        result.status = bc_modem_acknowledge(&session);
        break;
    case Operation::hangUp:
        result.status = bc_modem_hang_up(&session);
        break;
    case Operation::receivePacket:
        result.received = true;
        result.status = bc_modem_read_block(&session, result.bytes.data());
        break;
    }
    return result;
}
