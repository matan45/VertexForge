#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

namespace render::history
{
    // Frame-time history helpers shared by the profiler UI's CPU and GPU
    // plots. Pure functions so the hitch heuristics stay unit-testable
    // without a device or window.

    inline float median(std::vector<float> values)
    {
        if (values.empty()) return 0.0f;
        size_t mid = values.size() / 2;
        std::nth_element(values.begin(), values.begin() + mid, values.end());
        if (values.size() % 2 != 0)
        {
            return values[mid];
        }
        float upper = values[mid];
        float lower = *std::max_element(values.begin(), values.begin() + mid);
        return (lower + upper) * 0.5f;
    }

    // Indices of frames spiking above factor x median. Frames at or below
    // minMs never count as hitches — a 0.2ms frame doubling is noise, not
    // a hitch.
    inline std::vector<size_t> findHitches(const std::vector<float>& values,
                                           float factor = 2.0f, float minMs = 1.0f)
    {
        std::vector<size_t> hitches;
        if (values.size() < 4) return hitches;

        float threshold = std::max(median(values) * factor, minMs);
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (values[i] > threshold)
            {
                hitches.push_back(i);
            }
        }
        return hitches;
    }
}
