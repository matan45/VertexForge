#pragma once

#include "BrushFalloff.hpp"
#include "BrushTypes.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// VK-1653: turning a polyline of world XZ points into brush dabs, and normalising per-dab
// strength so a STROKE means what a single dab means.
//
// Every terrain brush adds `falloff(d / r) * strength` per dab (brush_compute.glsl Raise/Lower,
// WeightBrushApplicator Paint/Erase). Dabs swept along a line overlap, so a point on the stroke
// centreline receives the sum of every dab whose footprint covers it. Dividing the per-dab
// strength by the PEAK of that sum along the centreline makes the stroke's maximum centreline
// effect equal the requested amount -- for one dab, a short stroke or a long one alike.
//
// Header-only and free of Terrain.dll symbols so Services and the CPU-only tests can use it.

namespace terrain
{
    // Total arc length of a polyline.
    inline float polylineLength(const std::vector<glm::vec2>& points)
    {
        double length = 0.0;
        for (size_t i = 1; i < points.size(); ++i)
            length += static_cast<double>(glm::length(points[i] - points[i - 1]));
        return static_cast<float>(length);
    }

    // How many dabs resamplePolyline will produce, computed without allocating them, so a caller can
    // enforce a budget first. Saturates instead of overflowing on absurd inputs.
    inline uint64_t resampledDabCount(float length, float maxSpacing)
    {
        if (!(length > 0.0f) || !(maxSpacing > 0.0f))
            return 1;
        const double segments = std::ceil(static_cast<double>(length) / static_cast<double>(maxSpacing));
        if (!(segments < 1.0e15))
            return std::numeric_limits<uint64_t>::max();
        return static_cast<uint64_t>(segments) + 1;
    }

    // Resamples a polyline at a uniform arc length of at most `maxSpacing`, endpoints included.
    //   - empty input                  -> no dabs
    //   - zero length (one point, or every point coincident) -> one dab at points[0]
    //   - otherwise N = ceil(L / maxSpacing) segments -> N + 1 dabs at arc length i * L / N
    // `actualSpacingOut` receives L / N (0 for a single dab).
    inline std::vector<glm::vec2> resamplePolyline(const std::vector<glm::vec2>& points,
                                                   float maxSpacing,
                                                   float* actualSpacingOut = nullptr)
    {
        if (actualSpacingOut)
            *actualSpacingOut = 0.0f;
        if (points.empty())
            return {};

        const float length = polylineLength(points);
        if (!(length > 0.0f) || !(maxSpacing > 0.0f))
            return {points.front()};

        const uint64_t dabCount = resampledDabCount(length, maxSpacing);
        const uint64_t segments = dabCount - 1;
        const double step = static_cast<double>(length) / static_cast<double>(segments);
        if (actualSpacingOut)
            *actualSpacingOut = static_cast<float>(step);

        std::vector<glm::vec2> dabs;
        dabs.reserve(static_cast<size_t>(dabCount));

        size_t segment = 1;            // current polyline segment is [segment - 1, segment]
        double segmentStart = 0.0;     // arc length at points[segment - 1]
        for (uint64_t i = 0; i <= segments; ++i)
        {
            const double target = (i == segments) ? static_cast<double>(length)
                                                  : static_cast<double>(i) * step;

            // Advance to the segment containing `target`. Zero-length segments are skipped here.
            while (segment < points.size())
            {
                const double segmentLength =
                    static_cast<double>(glm::length(points[segment] - points[segment - 1]));
                if (target <= segmentStart + segmentLength || segment + 1 == points.size())
                    break;
                segmentStart += segmentLength;
                ++segment;
            }

            if (i == segments)
            {
                dabs.push_back(points.back()); // exact endpoint, no accumulated drift
                continue;
            }

            const glm::vec2 a = points[segment - 1];
            const glm::vec2 b = points[segment];
            const double segmentLength = static_cast<double>(glm::length(b - a));
            const double t = segmentLength > 0.0
                ? std::clamp((target - segmentStart) / segmentLength, 0.0, 1.0)
                : 0.0;
            dabs.push_back(a + (b - a) * static_cast<float>(t));
        }
        return dabs;
    }

    // The brush's normalised distance between two XZ points: Euclidean for a circle, Chebyshev for a
    // square -- the same metric brush_compute.glsl and WeightBrushApplicator use.
    inline float brushNormalizedDistance(const glm::vec2& a, const glm::vec2& b, float radius,
                                         BrushShape shape)
    {
        const glm::vec2 delta = glm::abs(a - b);
        const float distance = (shape == BrushShape::Circle) ? glm::length(a - b)
                                                             : std::max(delta.x, delta.y);
        return distance / radius;
    }

    // Peak of the summed dab influence along the stroke centreline, probed at every dab centre and
    // every midpoint between consecutive dabs (the extrema of the overlap sum on a uniformly
    // resampled line). Matches the shaders' exclusive rim: a dab contributes only while d / r < 1.
    // Returns at least 1 for a non-empty stroke (a lone dab is its own peak) and 1 for none.
    inline float strokePeakOverlap(const std::vector<glm::vec2>& dabs, float radius,
                                   BrushFalloff falloff, BrushShape shape)
    {
        if (dabs.empty() || !(radius > 0.0f))
            return 1.0f;

        auto overlapAt = [&](const glm::vec2& probe) {
            double sum = 0.0;
            for (const glm::vec2& dab : dabs)
            {
                const float t = brushNormalizedDistance(probe, dab, radius, shape);
                if (t < 1.0f)
                    sum += static_cast<double>(applyFalloff(t, falloff));
            }
            return sum;
        };

        double peak = 0.0;
        for (size_t i = 0; i < dabs.size(); ++i)
        {
            peak = std::max(peak, overlapAt(dabs[i]));
            if (i + 1 < dabs.size())
                peak = std::max(peak, overlapAt((dabs[i] + dabs[i + 1]) * 0.5f));
        }
        return static_cast<float>(std::max(peak, 1.0));
    }

    // Overlap sum at a dab centre on an INFINITE straight stroke with dab spacing `spacing`:
    //   S = sum over integers k with |k| * spacing < radius of falloff(|k| * spacing / radius).
    // At spacing = radius / 4: Constant 7, Linear 4, Smooth 4, Sharp 5.25. Reference value for tests
    // and docs; the stroke code uses strokePeakOverlap, which also handles short and curved strokes.
    inline float centrelineOverlap(float spacing, float radius, BrushFalloff falloff)
    {
        if (!(spacing > 0.0f) || !(radius > 0.0f))
            return 1.0f;
        double sum = static_cast<double>(applyFalloff(0.0f, falloff));
        for (int64_t k = 1;; ++k)
        {
            const double offset = static_cast<double>(k) * static_cast<double>(spacing);
            if (offset >= static_cast<double>(radius))
                break;
            sum += 2.0 * static_cast<double>(applyFalloff(static_cast<float>(offset / radius), falloff));
        }
        return static_cast<float>(sum);
    }
}
