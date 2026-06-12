#pragma once

#include "VFXTypes.hpp"
#include <cmath>
#include <cstdint>
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
            if (burst.count <= 0 || burst.probability <= 0.0f)
                continue;

            if (burst.interval <= 0.0f || burst.cycles == 1)
            {
                // Single fire at burst.time
                if (burst.time >= prevTime && burst.time < newTime)
                {
                    if (burst.probability >= 1.0f || rand01() <= burst.probability)
                        total += static_cast<uint32_t>(burst.count);
                }
                continue;
            }

            // First cycle index whose fire time >= prevTime
            float relative = (prevTime - burst.time) / burst.interval;
            int64_t k = (relative <= 0.0f) ? 0 : static_cast<int64_t>(std::ceil(relative));

            int evaluated = 0;
            while (evaluated < BurstDefaults::MAX_CYCLES_PER_WINDOW)
            {
                if (burst.cycles > 0 && k >= burst.cycles)
                    break;

                float fireTime = burst.time + static_cast<float>(k) * burst.interval;
                if (fireTime >= newTime)
                    break;

                if (fireTime >= prevTime)
                {
                    if (burst.probability >= 1.0f || rand01() <= burst.probability)
                        total += static_cast<uint32_t>(burst.count);
                }

                ++k;
                ++evaluated;
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
