#ifndef BC_COMPUTER_PLAYER_H
#define BC_COMPUTER_PLAYER_H
#include "search.h"
#include <atomic>
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
    explicit ComputerPlayer(uint32_t &state) : random_state(state) {
    }
    /* Native lifetime guard; no original entry point. Finish the worker before
     * destroying the cancellation flag or any state it can still read. */
    ~ComputerPlayer();
    void start(const BCGame &, const std::vector<BCGame> &past, bool book_eligible, unsigned level,
               int32_t seconds, bool hint);
    std::optional<ComputerResult> take_result();
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
        uint32_t random_state;
    };
    std::future<WorkerResult> worker;
    std::atomic<int> request{0};
    uint32_t &random_state;
    BCSearchSession search_session{};
    static uint64_t milliseconds(void *);
    static int poll(void *);
};
#endif
