#ifndef BC_COMPUTER_PLAYER_H
#define BC_COMPUTER_PLAYER_H

#include "search.h"
#include <atomic>
#include <cstdint>
#include <future>
#include <optional>
#include <vector>

struct ComputerResult {
    BCSearchResult search{};
    bool hint = false;
};

/* Native host boundary: a private board copy lets the recovered search run
 * without blocking SDL or exposing partially searched positions to the UI. */
class ComputerPlayer {
  public:
    /* Native ownership boundary: share the original RNG with animation fades.
     * Search publishes its advanced state only after the worker is joined. */
    explicit ComputerPlayer(uint32_t &sharedRandomState) : randomState(sharedRandomState) {
    }
    /* Native lifetime guard; no original entry point. Finish the worker before
     * destroying the cancellation flag or any state it can still read. */
    ~ComputerPlayer();
    void start(const BCGame &game, const std::vector<BCGame> &pastPositions, bool bookEligible,
               unsigned level, int32_t seconds, bool hint);

    /* Nonblocking handoff; consuming a result publishes its advanced RNG state. */
    std::optional<ComputerResult> takeResult();
    /* Cancel and join the worker before switching game sessions. */
    void cancel();
    void force();
    /* Native status query; no original entry point. A ready result remains busy
     * until the UI consumes it, preventing overlapping searches. */
    bool busy() const {
        return worker.valid();
    }

  private:
    struct WorkerResult {
        ComputerResult result;
        uint32_t randomState;
    };
    std::future<WorkerResult> worker;
    std::atomic<int> request{0};
    uint32_t &randomState;
    BCSearchSession searchSession{};
    static uint64_t milliseconds(void *context);
    static int poll(void *context);
};

#endif
