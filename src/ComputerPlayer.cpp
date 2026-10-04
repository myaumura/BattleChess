#include "ComputerPlayer.h"
#include "book.h"
#include "original.hpp"
#include <chrono>
#include <stdexcept>

namespace {
    /* Native record comparison; related to EQMOVE, file offset 0x4c4c.
     * Keep every original move field when resolving opening-book ordinals. */
    bool sameMove(BCMove left, BCMove right) {
        return left.to == right.to && left.from == right.from && left.special == right.special &&
               left.piece == right.piece && left.captured == right.captured;
    }

    /* Native history adapter for FUN_00005fe0 (file offset 0x5fe0).
     * Recover original INITMOVG ordinals from the real session snapshots; loaded
     * and edited boards are excluded by the caller because their history is absent. */
    std::optional<std::vector<uint8_t>> openingHistory(const BCGame &game,
                                                       const std::vector<BCGame> &pastPositions) {
        if (pastPositions.size() >= 200)
            return std::nullopt;
        std::vector<uint8_t> ordinals;
        for (size_t ply = 0; ply < pastPositions.size(); ++ply) {
            const BCGame &after = ply + 1 < pastPositions.size() ? pastPositions[ply + 1] : game;
            if (!after.history_count)
                return std::nullopt;
            BCMove played = after.history[after.history_count - 1];
            BCMove candidates[BC_GAME_MOVE_CAPACITY];
            size_t count = bc_game_pseudo_moves(&pastPositions[ply], candidates);
            size_t ordinal = 0;
            while (ordinal < count && !sameMove(candidates[ordinal], played))
                ++ordinal;
            if (ordinal >= count || ordinal >= 63)
                return std::nullopt;
            ordinals.push_back(uint8_t(ordinal));
        }
        return ordinals;
    }
} // namespace

/* Native monotonic clock; no original entry point. Replace TickCount-derived
 * elapsed time without depending on UI-thread SDL calls inside the search. */
uint64_t ComputerPlayer::milliseconds(void *) {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

/* Native command bridge; related to the original search's event polling.
 * Atomics carry force/cancel requests without sharing mutable chess state. */
int ComputerPlayer::poll(void *context) {
    return static_cast<ComputerPlayer *>(context)->request.load();
}

/* Native destructor; no original entry point. Cancellation must outlive the
 * worker, even if an exception exits the application before its result is read. */
ComputerPlayer::~ComputerPlayer() {
    request.store(-1);
    if (worker.valid())
        worker.wait();
}

/* Native coordinator for original computer setup 0x5b7c and FINDHINT 0x6794.
 * Novice and hints use depth two. Higher levels try FINDOPEN, then FINDMOVE;
 * a missing book continuation always reaches the original search, not a fallback AI. */
void ComputerPlayer::start(const BCGame &game, const std::vector<BCGame> &pastPositions,
                           bool bookEligible, unsigned level, int32_t seconds, bool hint) {
    if (busy())
        throw std::runtime_error("A computer search is already active");
    auto ordinals = bookEligible && level != 0 ? openingHistory(game, pastPositions) : std::nullopt;
    request.store(0);
    uint32_t seed = randomState;
    /* Native worker closure; no original entry point. Only the copied board,
     * copied history ordinals and atomic request flag cross the thread boundary. */
    worker = std::async(std::launch::async, [this, game, ordinals, level, seconds, hint, seed]() {
        WorkerResult output{};
        output.result.hint = hint;
        output.randomState = seed;
        auto &result = output.result.search;
        uint8_t ordinal = 0;
        if (ordinals &&
            book_move(original_opening_book, sizeof original_opening_book, ordinals->data(),
                      ordinals->size(), &output.randomState, &ordinal)) {
            BCMove candidates[BC_GAME_MOVE_CAPACITY];
            size_t count = bc_game_pseudo_moves(&game, candidates);
            if (ordinal >= count)
                throw std::runtime_error("Original opening book returned an invalid ordinal");
            result.move = candidates[ordinal];
            result.variation[0] = result.move;
            result.has_move = 1;
        } else {
            BCSearchLimits limits{hint || level == 0 ? 2U : 23U,
                                  0,
                                  seconds,
                                  milliseconds,
                                  poll,
                                  this,
                                  &searchSession};
            bc_search_find(&game, &limits, &output.randomState, &result);
        }
        if (request.load() < 0)
            result.cancelled = 1;
        return output;
    });
}

/* Native result handoff; no original entry point. Publish a complete result
 * and its advanced original RNG state only after the worker has finished. */
std::optional<ComputerResult> ComputerPlayer::takeResult() {
    if (!worker.valid() || worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    WorkerResult output = worker.get();
    randomState = output.randomState;
    return output.result;
}

/* Native cancellation; no original entry point. Join before changing a session,
 * so a stale result can never play into a newly loaded, edited or reset board. */
void ComputerPlayer::cancel() {
    request.store(-1);
    if (worker.valid()) {
        WorkerResult output = worker.get();
        randomState = output.randomState;
    }
}

/* Native command adapter for Force Move. The search's original stop points
 * choose the current best result; this does not substitute a different move. */
void ComputerPlayer::force() {
    request.store(1);
}
