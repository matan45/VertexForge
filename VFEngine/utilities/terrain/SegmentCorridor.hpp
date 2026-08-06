#pragma once

#include <glm/glm.hpp>

namespace terrain
{
    struct SegmentProjection
    {
        float t = 0.0f;
        float distance = 0.0f;
    };

    // Degenerate-segment policy intentionally belongs to the caller because ramp and
    // spline tools use different length thresholds.
    inline SegmentProjection projectOntoSegment(
        const glm::vec2& point,
        const glm::vec2& a,
        const glm::vec2& b,
        float segLength)
    {
        glm::vec2 segDir = b - a;
        glm::vec2 segNorm = segDir / segLength;
        float t = glm::dot(point - a, segNorm) / segLength;
        t = glm::clamp(t, 0.0f, 1.0f);

        glm::vec2 closest = a + segDir * t;
        float distance = glm::length(point - closest);
        return {t, distance};
    }

    inline float corridorBlend(float distance, float halfWidth, float falloffWidth)
    {
        float blend = 1.0f;
        if (distance > halfWidth && falloffWidth > 0.0f)
        {
            float falloffT = (distance - halfWidth) / falloffWidth;
            blend = 1.0f - falloffT * falloffT * (3.0f - 2.0f * falloffT);
        }
        return blend;
    }
}
