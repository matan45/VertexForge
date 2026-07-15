#pragma once

// VK-1514: pure, AL-free bus-meter aggregation. Kept header-only so the CPU-only
// Tests target can validate power summing, bus-tree propagation, and peak hold without
// linking the Audio DLL. This estimates pre-effects RMS under an uncorrelated-source
// assumption; it is not a tap of OpenAL's final output.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace core::audio::metering
{
    inline constexpr float kPeakDecayPerSec = 0.6f;

    struct BusNode
    {
        uint32_t parentId = 0;
        float volume = 1.0f;
        float effectiveVolume = 1.0f;
        bool muted = false;
        bool soloed = false;
    };

    struct Entry
    {
        float rms = 0.0f;
        float peakHold = 0.0f;
    };

    inline float finiteNonNegative(float value)
    {
        return std::isfinite(value) && value > 0.0f ? value : 0.0f;
    }

    // directPower[i] contains user-volume-adjusted power assigned directly to bus i.
    // outRms[i] is post-own-fader/pre-parent in normal routing. During the engine's
    // current solo-orphan exception, a soloed bus uses its actual effective gain and
    // propagates that audible power unchanged into ancestor meters.
    inline void accumulateBusRms(std::span<const BusNode> nodes,
                                 std::span<const float> directPower,
                                 std::span<float> outRms)
    {
        std::fill(outRms.begin(), outRms.end(), 0.0f);
        const std::size_t count = std::min({nodes.size(), directPower.size(), outRms.size()});
        if (count == 0)
            return;

        const bool anySoloed = std::any_of(nodes.begin(), nodes.begin() + count,
            [](const BusNode& node) { return node.soloed; });

        std::vector<float> outPower(count, 0.0f);
        std::vector<std::uint32_t> visitStamp(count, 0);
        std::uint32_t stamp = 0;

        for (std::size_t origin = 0; origin < count; ++origin)
        {
            float power = finiteNonNegative(directPower[origin]);
            if (power == 0.0f)
                continue;

            if (anySoloed)
            {
                if (!nodes[origin].soloed)
                    continue;
                const float gain = finiteNonNegative(nodes[origin].effectiveVolume);
                power *= gain * gain;
            }

            if (++stamp == 0)
            {
                std::fill(visitStamp.begin(), visitStamp.end(), 0);
                ++stamp;
            }

            std::size_t current = origin;
            while (current < count && visitStamp[current] != stamp)
            {
                visitStamp[current] = stamp;
                if (!anySoloed)
                {
                    const float gain = nodes[current].muted
                        ? 0.0f : finiteNonNegative(nodes[current].volume);
                    power *= gain * gain;
                }

                outPower[current] += power;
                const std::size_t parent = nodes[current].parentId;
                if (parent == current || parent >= count)
                    break;
                current = parent;
            }
        }

        for (std::size_t i = 0; i < count; ++i)
            outRms[i] = std::sqrt(finiteNonNegative(outPower[i]));
    }

    inline float decayPeakHold(float previousHold, float rms, float deltaTime)
    {
        previousHold = finiteNonNegative(previousHold);
        rms = finiteNonNegative(rms);
        if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
            deltaTime = 0.0f;
        return std::max(rms, std::max(0.0f, previousHold - kPeakDecayPerSec * deltaTime));
    }
}
