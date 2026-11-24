#include "IdleAnimator.h"
#include "animation_data.hpp"
#include "original.hpp"
#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    // The archive's idle group: twelve bishop scripts. Scripts 0..3 (28-29 frames) and
    // 4..7 (13 frames) start from standing shapes 0..3; 8..11 repeat 4..7 byte for byte.
    constexpr unsigned kScriptsPerShape = 2;

    const GeneratedAnimGroup& idleGroup()
    {
        for (const auto& group : animation_groups)
            if (std::strcmp(group.name, "idle") == 0)
            {
                if (group.count < 8)
                    throw std::runtime_error("Idle animation group is incomplete");
                return group;
            }
        throw std::runtime_error("Missing idle animation group");
    }

    const GeneratedAnimClip& idleClip(int script)
    {
        return animation_clips[idleGroup().first + unsigned(script)];
    }

    /* A bishop rests in display variant 1, 3, 5 or 7 (by side and board half), which the
     * perspective table maps to standing shapes 0..3: the idle scripts' base shapes. */
    int bishopRestingShape(uint8_t code)
    {
        if ((code & 7) != 1)
            return -1;
        int shape = perspective_shapes[code & 63] - 17;
        return shape >= 0 && shape < 4 ? shape : -1;
    }
}

void IdleAnimator::setEnabled(bool enabled, uint64_t now)
{
    on = enabled;
    stop(now);
}

void IdleAnimator::interrupt(uint64_t now)
{
    stop(now);
}

void IdleAnimator::schedule(uint64_t now)
{
    std::uniform_int_distribution<uint64_t> delay(kMinDelay, kMaxDelay);
    nextAt = now + delay(random);
}

void IdleAnimator::stop(uint64_t now)
{
    square = script = -1;
    schedule(now);
}

bool IdleAnimator::start(uint64_t now, const uint8_t display[64])
{
    std::vector<int> bishops;
    for (int i = 0; i < 64; ++i)
        if (bishopRestingShape(display[i]) >= 0)
            bishops.push_back(i);
    if (bishops.empty())
        return false;
    square = bishops[std::uniform_int_distribution<size_t>(0, bishops.size() - 1)(random)];
    squareCode = display[square];
    unsigned variant = std::uniform_int_distribution<unsigned>(0, kScriptsPerShape - 1)(random);
    script = bishopRestingShape(squareCode) + int(variant) * 4;
    startedAt = now;
    frameIndex = 0;
    ++clipsStarted;
    return true;
}

void IdleAnimator::update(uint64_t now, bool allowed, const uint8_t display[64], double speed)
{
    if (!on)
        return;
    // Wait a full quiet period after anything that blocks idling, such as the computer's turn.
    if (!allowed)
    {
        if (square >= 0)
            stop(now);
        wasAllowed = false;
        return;
    }
    if (!wasAllowed)
    {
        wasAllowed = true;
        stop(now);
        return;
    }
    if (square >= 0)
    {
        // The board changed under the clip (for example a remote move): drop it.
        if (display[square] != squareCode)
        {
            stop(now);
            return;
        }
        auto elapsed = uint64_t(double(now - startedAt) * speed);
        frameIndex = unsigned(elapsed / kFrameMilliseconds);
        if (frameIndex >= idleClip(script).count)
            stop(now);
        return;
    }
    if (now >= nextAt && !start(now, display))
        schedule(now);
}

std::optional<IdleAnimator::Frame> IdleAnimator::current() const
{
    if (!on || square < 0)
        return std::nullopt;
    return Frame{square, idleClip(script).first + frameIndex, (squareCode & 64) != 0};
}
