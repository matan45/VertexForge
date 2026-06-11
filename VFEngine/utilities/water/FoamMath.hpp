#pragma once

#include <glm/glm.hpp>
#include <cmath>

namespace water
{
    // CPU mirror of the persistent-foam math in resources/shaders/water/ocean_merge.glsl.
    // Keep both in sync — these functions exist so the behavior (decay half-life, UV wrap,
    // persistence blend) is unit-testable without a GPU.

    // Exponential decay of accumulated foam over dt seconds
    inline float foamDecayStep(float foam, float decayPerSecond, float deltaTime)
    {
        return foam * std::exp(-decayPerSecond * deltaTime);
    }

    // Where this texel's foam came from last frame: drift backwards along the horizontal
    // chop displacement (meters), normalized into patch UV space
    inline glm::vec2 advectedFoamUV(const glm::vec2& uv, const glm::vec2& horizontalDisplacement,
                                    float patchSize, float deltaTime)
    {
        return uv - horizontalDisplacement / glm::max(patchSize, 0.0001f) * deltaTime;
    }

    // The FFT patch is periodic; the GPU repeat sampler wraps for free, this is the CPU twin
    inline glm::vec2 wrapUV(const glm::vec2& uv)
    {
        return glm::fract(glm::fract(uv) + glm::vec2(1.0f));
    }

    // Blend instantaneous Jacobian foam with the surviving history. persistence = 0 reduces
    // to pure instantaneous foam; fresh foam is never dimmer than the instantaneous value.
    inline float combineFoam(float instantFoam, float previousFoam, float persistence)
    {
        return glm::max(instantFoam, glm::mix(instantFoam, previousFoam, persistence));
    }
}
