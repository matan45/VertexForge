#pragma once

#include "VFXTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace vfx
{
    // A scheduled burst of particles on top of the continuous spawn rate.
    // The first cycle fires at `time` (seconds since emission start), then
    // repeats every `interval` seconds for `cycles` cycles (0 = repeat forever).
    struct VFXBurst
    {
        float time = 0.0f;
        int32_t count = 30;
        int32_t cycles = 1;
        float interval = 0.5f;
        float probability = 1.0f;
    };

    namespace BurstDefaults
    {
        inline constexpr int MAX_BURSTS = 8;
        inline constexpr float TIME = 0.0f;
        inline constexpr int COUNT = 30;
        inline constexpr int CYCLES = 1;
        inline constexpr float INTERVAL = 0.5f;
        inline constexpr float PROBABILITY = 1.0f;
        // Safety bound on cycles evaluated per frame window (pathological tiny intervals)
        inline constexpr int MAX_CYCLES_PER_WINDOW = 64;
    }

    inline std::string burstPropName(int index, const char* field)
    {
        return "burst" + std::to_string(index) + field;
    }

    namespace detail
    {
        template <typename Rand01>
        uint32_t evaluateOneBurst(const VFXBurst& burst, float prevTime, float newTime,
                                  Rand01& rand01, int& remainingEvaluations)
        {
            if (newTime <= prevTime || remainingEvaluations <= 0 ||
                burst.count <= 0 || burst.probability <= 0.0f ||
                !std::isfinite(burst.time) || !std::isfinite(burst.interval) ||
                !std::isfinite(burst.probability))
            {
                return 0;
            }

            uint32_t total = 0;
            if (burst.interval <= 0.0f || burst.cycles == 1)
            {
                // Single fire at burst.time. In a looped schedule the shared budget
                // bounds the number of re-armed single fires across period slices.
                if (burst.time >= prevTime && burst.time < newTime)
                {
                    --remainingEvaluations;
                    if (burst.probability >= 1.0f || rand01() <= burst.probability)
                        total += static_cast<uint32_t>(burst.count);
                }
                return total;
            }

            // First cycle index whose fire time >= prevTime.
            float relative = (prevTime - burst.time) / burst.interval;
            int64_t k = (relative <= 0.0f) ? 0 : static_cast<int64_t>(std::ceil(relative));

            while (remainingEvaluations > 0)
            {
                if (burst.cycles > 0 && k >= burst.cycles)
                    break;

                float fireTime = burst.time + static_cast<float>(k) * burst.interval;
                if (fireTime >= newTime)
                    break;

                --remainingEvaluations;
                if (fireTime >= prevTime)
                {
                    if (burst.probability >= 1.0f || rand01() <= burst.probability)
                        total += static_cast<uint32_t>(burst.count);
                }

                ++k;
            }

            return total;
        }
    }

    // Stateless burst schedule evaluation: returns the number of particles due in
    // the half-open emission-time window [prevTime, newTime). Because the window is
    // half-open and consecutive frames tile the timeline, every cycle fires exactly
    // once regardless of frame rate. rand01() is invoked once per firing cycle for
    // the probability roll; pass a constant 0.0f generator for deterministic tests.
    template <typename Rand01>
    uint32_t evaluateBurstSpawns(const std::vector<VFXBurst>& bursts,
                                 float prevTime, float newTime, Rand01&& rand01)
    {
        if (newTime <= prevTime)
            return 0;

        uint32_t total = 0;
        for (const auto& burst : bursts)
        {
            int remainingEvaluations = BurstDefaults::MAX_CYCLES_PER_WINDOW;
            total += detail::evaluateOneBurst(
                burst, prevTime, newTime, rand01, remainingEvaluations);
        }

        return total;
    }

    // Resolves the period used to re-arm finite bursts. Burst looping is OPT-IN
    // (VK-1524 review #5): only a positive authored duration wraps finite bursts.
    // Zero (the default) and any non-finite/negative value disable wrapping, so a
    // finite burst fires exactly once on the absolute emission timeline — matching
    // pre-loop behavior and not silently re-arming one-shot bursts on looping
    // emitters. Infinite bursts (cycles <= 0) still repeat via their own interval
    // inside evaluateBurstSpawns, independent of this period.
    inline float resolveBurstLoopPeriod(const std::vector<VFXBurst>& bursts,
                                        float configuredDuration, float emitterLifetime)
    {
        (void)bursts;         // period is no longer derived from the burst schedule
        (void)emitterLifetime; // or the emitter lifetime — looping is explicit only
        if (std::isfinite(configuredDuration) && configuredDuration > 0.0f)
            return configuredDuration;
        return 0.0f; // 0 / invalid => no burst re-arm (finite bursts fire once)
    }

    // Loop-aware stateless evaluation. Infinite schedules (cycles <= 0) retain
    // their absolute timeline; finite schedules are evaluated in local period
    // slices. One budget is shared across every slice for each authored burst.
    template <typename Rand01>
    uint32_t evaluateBurstSpawnsLooped(const std::vector<VFXBurst>& bursts,
                                       float prevTime, float newTime, float loopPeriod,
                                       Rand01&& rand01)
    {
        if (newTime <= prevTime)
            return 0;
        if (!std::isfinite(loopPeriod) || loopPeriod <= 0.0f)
            return evaluateBurstSpawns(bursts, prevTime, newTime, rand01);

        uint32_t total = 0;
        const double period = static_cast<double>(loopPeriod);
        const double windowEnd = static_cast<double>(newTime);

        for (const auto& burst : bursts)
        {
            int remainingEvaluations = BurstDefaults::MAX_CYCLES_PER_WINDOW;
            if (burst.cycles <= 0)
            {
                total += detail::evaluateOneBurst(
                    burst, prevTime, newTime, rand01, remainingEvaluations);
                continue;
            }

            double current = std::max(0.0, static_cast<double>(prevTime));
            int slices = 0;
            while (current < windowEnd && remainingEvaluations > 0 &&
                   slices < BurstDefaults::MAX_CYCLES_PER_WINDOW)
            {
                double periodIndex = std::floor(current / period);
                double periodStart = periodIndex * period;
                double localStart = current - periodStart;

                const double seamTolerance = std::numeric_limits<double>::epsilon() *
                    std::max({1.0, std::abs(current), period}) * 4.0;
                if (localStart < 0.0 && localStart >= -seamTolerance)
                    localStart = 0.0;
                if (period - localStart <= seamTolerance)
                {
                    periodStart += period;
                    localStart = 0.0;
                }
                localStart = std::clamp(localStart, 0.0, period);

                const double sliceEnd = std::min(windowEnd, periodStart + period);
                if (!(sliceEnd > current))
                    break;

                const double localEnd = std::clamp(
                    localStart + (sliceEnd - current), localStart, period);
                total += detail::evaluateOneBurst(
                    burst, static_cast<float>(localStart), static_cast<float>(localEnd),
                    rand01, remainingEvaluations);

                current = sliceEnd;
                ++slices;
            }
        }

        return total;
    }

    // Reads the burst list from flat Emitter-node properties:
    // "burstCount" (int) + "burst<i>Time/Count/Cycles/Interval/Probability".
    inline std::vector<VFXBurst> loadBurstsFromNode(const VFXNode& node)
    {
        auto getFloatProp = [&node](const std::string& name, float defaultValue) -> float {
            auto it = node.properties.find(name);
            if (it != node.properties.end())
                if (auto* val = std::get_if<float>(&it->second.value))
                    return *val;
            return defaultValue;
        };
        auto getIntProp = [&node](const std::string& name, int32_t defaultValue) -> int32_t {
            auto it = node.properties.find(name);
            if (it != node.properties.end())
                if (auto* val = std::get_if<int32_t>(&it->second.value))
                    return *val;
            return defaultValue;
        };

        int burstCount = getIntProp("burstCount", 0);
        if (burstCount < 0) burstCount = 0;
        if (burstCount > BurstDefaults::MAX_BURSTS) burstCount = BurstDefaults::MAX_BURSTS;

        std::vector<VFXBurst> bursts;
        bursts.reserve(static_cast<size_t>(burstCount));
        for (int i = 0; i < burstCount; ++i)
        {
            VFXBurst burst;
            burst.time = getFloatProp(burstPropName(i, "Time"), BurstDefaults::TIME);
            burst.count = getIntProp(burstPropName(i, "Count"), BurstDefaults::COUNT);
            burst.cycles = getIntProp(burstPropName(i, "Cycles"), BurstDefaults::CYCLES);
            burst.interval = getFloatProp(burstPropName(i, "Interval"), BurstDefaults::INTERVAL);
            burst.probability = getFloatProp(burstPropName(i, "Probability"), BurstDefaults::PROBABILITY);
            bursts.push_back(burst);
        }
        return bursts;
    }

    // Writes the burst list back as flat Emitter-node properties (inverse of loadBurstsFromNode).
    inline void storeBurstsToNode(VFXNode& node, const std::vector<VFXBurst>& bursts)
    {
        int count = static_cast<int>(bursts.size());
        if (count > BurstDefaults::MAX_BURSTS) count = BurstDefaults::MAX_BURSTS;

        node.properties["burstCount"] = VFXProperty{
            "burstCount", VFXPropertyType::Int, count,
            0.0f, static_cast<float>(BurstDefaults::MAX_BURSTS)};

        for (int i = 0; i < count; ++i)
        {
            const auto& burst = bursts[static_cast<size_t>(i)];
            node.properties[burstPropName(i, "Time")] = VFXProperty{
                burstPropName(i, "Time"), VFXPropertyType::Float, burst.time, 0.0f, 60.0f};
            node.properties[burstPropName(i, "Count")] = VFXProperty{
                burstPropName(i, "Count"), VFXPropertyType::Int, burst.count, 0.0f, 10000.0f};
            node.properties[burstPropName(i, "Cycles")] = VFXProperty{
                burstPropName(i, "Cycles"), VFXPropertyType::Int, burst.cycles, 0.0f, 100.0f};
            node.properties[burstPropName(i, "Interval")] = VFXProperty{
                burstPropName(i, "Interval"), VFXPropertyType::Float, burst.interval, 0.0f, 60.0f};
            node.properties[burstPropName(i, "Probability")] = VFXProperty{
                burstPropName(i, "Probability"), VFXPropertyType::Float, burst.probability, 0.0f, 1.0f};
        }

        // Drop stale trailing burst properties from previous larger lists
        for (int i = count; i < BurstDefaults::MAX_BURSTS; ++i)
        {
            node.properties.erase(burstPropName(i, "Time"));
            node.properties.erase(burstPropName(i, "Count"));
            node.properties.erase(burstPropName(i, "Cycles"));
            node.properties.erase(burstPropName(i, "Interval"));
            node.properties.erase(burstPropName(i, "Probability"));
        }
    }
}
