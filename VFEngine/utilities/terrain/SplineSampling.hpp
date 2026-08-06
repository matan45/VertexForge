#pragma once

#include "SplineTypes.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <vector>

namespace terrain
{
    inline glm::vec3 evaluateCatmullRom(
        const glm::vec3& p0,
        const glm::vec3& p1,
        const glm::vec3& p2,
        const glm::vec3& p3,
        float t)
    {
        float t2 = t * t;
        float t3 = t2 * t;

        return 0.5f * (
            (2.0f * p1) +
            (-p0 + p2) * t +
            (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
        );
    }

    inline std::vector<glm::vec3> sampleSplineCurve(
        const std::vector<SplineControlPoint>& points,
        float stepSize)
    {
        std::vector<glm::vec3> samples;

        if (points.size() < 2)
            return samples;

        // For 2 points, just linear
        if (points.size() == 2)
        {
            float dist = glm::length(points[1].position - points[0].position);
            int steps = std::max(1, static_cast<int>(dist / stepSize));
            for (int i = 0; i <= steps; ++i)
            {
                float t = static_cast<float>(i) / static_cast<float>(steps);
                samples.push_back(glm::mix(points[0].position, points[1].position, t));
            }
            return samples;
        }

        // Catmull-Rom: iterate segments between consecutive points
        for (size_t seg = 0; seg + 1 < points.size(); ++seg)
        {
            // Clamp indices for boundary segments
            size_t i0 = (seg > 0) ? seg - 1 : 0;
            size_t i1 = seg;
            size_t i2 = seg + 1;
            size_t i3 = (seg + 2 < points.size()) ? seg + 2 : points.size() - 1;

            const glm::vec3& p0 = points[i0].position;
            const glm::vec3& p1 = points[i1].position;
            const glm::vec3& p2 = points[i2].position;
            const glm::vec3& p3 = points[i3].position;

            float segLen = glm::length(p2 - p1);
            int steps = std::max(1, static_cast<int>(segLen / stepSize));

            for (int i = 0; i < steps; ++i)
            {
                float t = static_cast<float>(i) / static_cast<float>(steps);
                samples.push_back(evaluateCatmullRom(p0, p1, p2, p3, t));
            }
        }

        // Add final point
        samples.push_back(points.back().position);
        return samples;
    }
}
