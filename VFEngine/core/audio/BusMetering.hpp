#pragma once

// VK-1514: pure, AL-free bus-meter aggregation. Kept header-only so the CPU-only
// Tests target can validate power summing, bus-tree propagation, and peak hold without
// linking the Audio DLL. This estimates pre-effects RMS under an uncorrelated-source
// assumption; it is not a tap of OpenAL's final output.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace core::audio::metering
{
    inline constexpr float kPeakDecayPerSec = 0.6f;
    inline constexpr std::size_t kMaxTraversalDepth = 32;

    struct BusNode
    {
        uint32_t parentId = 0;
        float volume = 1.0f;
        float effectiveVolume = 1.0f; // detects the engine's zero-parent solo bypass edge
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

    inline float applyPowerGain(float power, float gain)
    {
        power = finiteNonNegative(power);
        gain = finiteNonNegative(gain);
        const double scaled = static_cast<double>(power)
            * static_cast<double>(gain) * static_cast<double>(gain);
        return static_cast<float>(std::min(
            scaled, static_cast<double>(std::numeric_limits<float>::max())));
    }

    inline float addPowerSaturated(float lhs, float rhs)
    {
        const double sum = static_cast<double>(finiteNonNegative(lhs))
            + static_cast<double>(finiteNonNegative(rhs));
        return static_cast<float>(std::min(
            sum, static_cast<double>(std::numeric_limits<float>::max())));
    }

    // directPower[i] contains user-volume-adjusted power assigned directly to bus i.
    // outRms[i] is post-own-fader/pre-parent. During the engine's solo-orphan
    // exception, power crosses the first zero-effective parent unchanged so ancestor
    // meters still represent the source that OpenAL is actually playing.
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

        for (std::size_t origin = 0; origin < count; ++origin)
        {
            float power = finiteNonNegative(directPower[origin]);
            if (power == 0.0f)
                continue;

            if (anySoloed && !nodes[origin].soloed)
                continue;

            std::array<std::size_t, kMaxTraversalDepth> visited{};
            std::size_t depth = 0;
            std::size_t current = origin;
            bool soloBypassedParent = false;
            while (current < count && depth < visited.size())
            {
                if (std::find(visited.begin(), visited.begin() + depth, current)
                    != visited.begin() + depth)
                {
                    break;
                }
                visited[depth++] = current;

                if (nodes[current].muted)
                {
                    // Mute always wins over solo. A child meter is still pre-parent, but
                    // the muted node and every ancestor receive zero power.
                    power = 0.0f;
                }
                else if (anySoloed)
                {
                    if (current == origin)
                    {
                        power = applyPowerGain(power, nodes[current].volume);
                    }
                    else if (!soloBypassedParent)
                    {
                        // A zero parent effective gain is the engine's solo-orphan edge:
                        // the child keeps its own gain and bypasses all higher faders.
                        if (!nodes[current].soloed
                            || finiteNonNegative(nodes[current].effectiveVolume) == 0.0f)
                        {
                            soloBypassedParent = true;
                        }
                        else
                        {
                            power = applyPowerGain(power, nodes[current].volume);
                        }
                    }
                }
                else
                {
                    const float gain = nodes[current].muted
                        ? 0.0f : finiteNonNegative(nodes[current].volume);
                    power = applyPowerGain(power, gain);
                }

                // outRms is power scratch until the final square-root pass. Keeping the
                // traversal state on the stack avoids heap traffic on the 200 Hz audio tick.
                outRms[current] = addPowerSaturated(outRms[current], power);
                const std::size_t parent = nodes[current].parentId;
                if (parent == current || parent >= count)
                    break;
                current = parent;
            }
        }

        for (std::size_t i = 0; i < count; ++i)
            outRms[i] = std::sqrt(finiteNonNegative(outRms[i]));
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
