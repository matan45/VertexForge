#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace vfx
{
    struct VFXCurveKey
    {
        float time = 0.0f;       // [0..1] normalized lifetime
        float value = 0.0f;
        float inTangent = 0.0f;  // incoming tangent
        float outTangent = 0.0f; // outgoing tangent
    };

    struct VFXCurve
    {
        std::vector<VFXCurveKey> keys;

        float evaluate(float t) const;

        static VFXCurve fromStartEnd(float startValue, float endValue);
        static VFXCurve constant(float value);

        bool empty() const { return keys.empty(); }
    };

    struct VFXGradientStop
    {
        float position = 0.0f;               // [0..1]
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // RGBA HDR
    };

    struct VFXGradient
    {
        std::vector<VFXGradientStop> stops;

        glm::vec4 evaluate(float t) const;

        static VFXGradient fromStartEnd(const glm::vec4& startColor, const glm::vec4& endColor);

        bool empty() const { return stops.empty(); }
    };
}
