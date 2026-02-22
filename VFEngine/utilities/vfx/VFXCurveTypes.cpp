#include "VFXCurveTypes.hpp"
#include <cmath>

namespace vfx
{
    float VFXCurve::evaluate(float t) const
    {
        if (keys.empty())
            return 0.0f;

        t = glm::clamp(t, 0.0f, 1.0f);

        if (keys.size() == 1)
            return keys[0].value;

        if (t <= keys.front().time)
            return keys.front().value;

        if (t >= keys.back().time)
            return keys.back().value;

        for (size_t i = 0; i < keys.size() - 1; ++i)
        {
            const auto& k0 = keys[i];
            const auto& k1 = keys[i + 1];

            if (t >= k0.time && t <= k1.time)
            {
                float dt = k1.time - k0.time;
                if (dt < 1e-6f)
                    return k0.value;

                float localT = (t - k0.time) / dt;

                // Cubic Hermite interpolation using tangents
                float t2 = localT * localT;
                float t3 = t2 * localT;

                float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
                float h10 = t3 - 2.0f * t2 + localT;
                float h01 = -2.0f * t3 + 3.0f * t2;
                float h11 = t3 - t2;

                return h00 * k0.value + h10 * dt * k0.outTangent +
                       h01 * k1.value + h11 * dt * k1.inTangent;
            }
        }

        return keys.back().value;
    }

    VFXCurve VFXCurve::fromStartEnd(float startValue, float endValue)
    {
        VFXCurve curve;
        curve.keys = {
            {0.0f, startValue, 0.0f, 0.0f},
            {1.0f, endValue,   0.0f, 0.0f}
        };
        return curve;
    }

    VFXCurve VFXCurve::constant(float value)
    {
        VFXCurve curve;
        curve.keys = {
            {0.0f, value, 0.0f, 0.0f},
            {1.0f, value, 0.0f, 0.0f}
        };
        return curve;
    }

    glm::vec4 VFXGradient::evaluate(float t) const
    {
        if (stops.empty())
            return glm::vec4(1.0f);

        t = glm::clamp(t, 0.0f, 1.0f);

        if (stops.size() == 1)
            return stops[0].color;

        if (t <= stops.front().position)
            return stops.front().color;

        if (t >= stops.back().position)
            return stops.back().color;

        for (size_t i = 0; i < stops.size() - 1; ++i)
        {
            const auto& s0 = stops[i];
            const auto& s1 = stops[i + 1];

            if (t >= s0.position && t <= s1.position)
            {
                float dp = s1.position - s0.position;
                if (dp < 1e-6f)
                    return s0.color;

                float localT = (t - s0.position) / dp;
                return glm::mix(s0.color, s1.color, localT);
            }
        }

        return stops.back().color;
    }

    VFXGradient VFXGradient::fromStartEnd(const glm::vec4& startColor, const glm::vec4& endColor)
    {
        VFXGradient gradient;
        gradient.stops = {
            {0.0f, startColor},
            {1.0f, endColor}
        };
        return gradient;
    }
}
