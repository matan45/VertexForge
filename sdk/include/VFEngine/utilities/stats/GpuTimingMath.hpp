#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace render::timing
{
    // GPU timestamp arithmetic shared by the render-graph profiler and the
    // whole-frame timer. Pure and Vulkan-free so the maths that decides what
    // the profiler UI reports stays unit-testable without a device.

    inline constexpr float EMA_ALPHA = 0.1f;

    // Timestamp counters are only `validBits` wide (the spec permits 36..64, and
    // guarantees bits outside that range read as zero), so the counter wraps at
    // 2^validBits. Subtracting modulo that width therefore reports a
    // wrap-straddling interval's real duration — at 36 bits and a ~1ns period the
    // counter wraps roughly every 69 seconds. No need to mask the inputs: the spec
    // already guarantees their high bits are zero.
    //
    // A delta landing in the upper half of the range is not a very long wrap — a
    // real interval is at most a frame — it is an inverted pair, from a driver
    // quirk or a query that was read without being written. Report 0 rather than
    // ~2^validBits ticks: one such sample would pin emaFrameGpuMs for seconds and
    // flatten the history plot's whole 120-frame window.
    inline float boundaryDeltaMs(uint64_t start, uint64_t end, double periodNs, uint32_t validBits)
    {
        if (periodNs <= 0.0 || validBits == 0) return 0.0f;

        const uint64_t mask = (validBits >= 64) ? ~uint64_t{0}
                                                : ((uint64_t{1} << validBits) - 1);
        const uint64_t ticks = (end - start) & mask;
        if (ticks > (mask >> 1)) return 0.0f;

        return static_cast<float>(static_cast<double>(ticks) * periodNs / 1'000'000.0);
    }

    // Boundary chain: N+1 bottom-of-pipe timestamps bracket N passes, so pass i
    // spans boundaries[i]..boundaries[i+1] and the total is simply first-to-last.
    // Unlike summing per-pass top-of-pipe -> bottom-of-pipe pairs, adjacent spans
    // cannot overlap, so the parts add up to the total instead of overshooting it
    // and every share is a true percentage.
    inline void passDeltasFromBoundaries(const std::vector<uint64_t>& boundaries,
                                         double periodNs, uint32_t validBits,
                                         std::vector<float>& outMs, float& outTotalMs)
    {
        outMs.clear();
        outTotalMs = 0.0f;
        if (boundaries.size() < 2) return;

        outMs.reserve(boundaries.size() - 1);
        for (size_t i = 0; i + 1 < boundaries.size(); ++i)
        {
            outMs.push_back(boundaryDeltaMs(boundaries[i], boundaries[i + 1], periodNs, validBits));
        }
        outTotalMs = boundaryDeltaMs(boundaries.front(), boundaries.back(), periodNs, validBits);
    }

    // Exponential moving average keyed by pass NAME rather than by position.
    // Graph passes are conditional (SSR, SSGI, Clouds, Upscale, the plugin
    // hooks), so a positional key blends one pass's history into another's
    // samples the moment any pass toggles. Seeding each entry from its own
    // first sample also stops a newly appearing pass from climbing out of zero
    // over the ~20-40 frames an alpha of 0.1 takes to converge.
    class NamedEma
    {
    public:
        float update(const std::string& name, float sample, uint64_t frame)
        {
            Entry& entry = entries[name];
            entry.value = entry.seeded ? (EMA_ALPHA * sample + (1.0f - EMA_ALPHA) * entry.value)
                                       : sample;
            entry.seeded = true;
            entry.lastSeenFrame = frame;
            return entry.value;
        }

        // Forget passes unseen for maxAge frames so a long session cannot
        // accumulate entries for passes that no longer exist.
        void prune(uint64_t frame, uint64_t maxAge)
        {
            std::erase_if(entries, [frame, maxAge](const auto& kv)
            {
                return frame > kv.second.lastSeenFrame + maxAge;
            });
        }

        void clear() { entries.clear(); }
        size_t size() const { return entries.size(); }

    private:
        struct Entry
        {
            float value = 0.0f;
            bool seeded = false;
            uint64_t lastSeenFrame = 0;
        };

        std::unordered_map<std::string, Entry> entries;
    };
}
