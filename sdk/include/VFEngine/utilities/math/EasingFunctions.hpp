#pragma once

#include <cmath>
#include <components/UIComponents.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace math
{
    inline float evaluateEasing(components::UIEasingFunction func, float t)
    {
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

        switch (func)
        {
        case components::UIEasingFunction::Linear:
            return t;

        case components::UIEasingFunction::EaseIn:
            return t * t;

        case components::UIEasingFunction::EaseOut:
            return 1.0f - (1.0f - t) * (1.0f - t);

        case components::UIEasingFunction::EaseInOut:
            return t * t * (3.0f - 2.0f * t);

        case components::UIEasingFunction::Bounce:
        {
            float n = 1.0f - t;
            if (n < 1.0f / 2.75f)
                return 1.0f - 7.5625f * n * n;
            else if (n < 2.0f / 2.75f)
            {
                n -= 1.5f / 2.75f;
                return 1.0f - (7.5625f * n * n + 0.75f);
            }
            else if (n < 2.5f / 2.75f)
            {
                n -= 2.25f / 2.75f;
                return 1.0f - (7.5625f * n * n + 0.9375f);
            }
            else
            {
                n -= 2.625f / 2.75f;
                return 1.0f - (7.5625f * n * n + 0.984375f);
            }
        }

        case components::UIEasingFunction::Elastic:
        {
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return std::pow(2.0f, -10.0f * t) * std::sin((t - 0.075f) * (2.0f * static_cast<float>(M_PI)) / 0.3f) + 1.0f;
        }

        default:
            return t;
        }
    }
}
