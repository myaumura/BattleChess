#ifndef BC_IDLE_ANIMATOR_H
#define BC_IDLE_ANIMATOR_H

#include <cstdint>
#include <optional>
#include <random>

/* Native idle extension; no original entry point. The Macintosh 1.0.2 executable never
 * loads the archive's idle block (see docs/ROADMAP.md), so the trigger, delays and piece
 * choice here are invented and opt-in. The clips themselves are the recovered bishop
 * scripts, each drawn on a bishop whose resting pose matches the clip's base shape.
 * Time is passed in milliseconds so checks can drive it without waiting. */
class IdleAnimator
{
public:
    // Quiet time before the first clip and between clips, in milliseconds.
    static constexpr uint64_t kMinDelay = 10000, kMaxDelay = 25000;
    // One frame per recovered animation step at 1x: six 60 Hz ticks.
    static constexpr uint64_t kFrameMilliseconds = 100;

    struct Frame
    {
        int square; // Display square of the bishop.
        unsigned frame; // Global animation frame index.
        bool white;
    };

    // A private generator: the shared original RNG (search, fades) is never drawn from.
    explicit IdleAnimator(uint32_t seed) : random(seed)
    {
    }

    bool enabled() const
    {
        return on;
    }

    void setEnabled(bool enabled, uint64_t now);
    // Any user input stops the clip and restarts the quiet period.
    void interrupt(uint64_t now);
    /* allowed: the 3D board is shown on a human turn with nothing else happening.
     * display: setup_display_board codes for the shown position. */
    void update(uint64_t now, bool allowed, const uint8_t display[64], double speed);
    std::optional<Frame> current() const;
    // Number of clips started; lets checks observe playback without timing assumptions.
    unsigned started() const
    {
        return clipsStarted;
    }

private:
    std::minstd_rand random;
    bool on = false, wasAllowed = false;
    uint64_t nextAt = 0, startedAt = 0;
    int square = -1, script = -1;
    uint8_t squareCode = 0;
    unsigned frameIndex = 0, clipsStarted = 0;

    void schedule(uint64_t now);
    void stop(uint64_t now);
    bool start(uint64_t now, const uint8_t display[64]);
};

#endif
