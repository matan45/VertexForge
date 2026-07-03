#pragma once

#include <cstdint>

namespace vfx
{
    enum class VarianceStream : uint32_t
    {
        Size = 0xA341316Cu,
        Lifetime = 0xC8013EA4u,
        Speed = 0xAD90777Du,
        Rotation = 0x7E95761Eu,
        AngularVelocity = 0x9E3779B9u,
        ColorValue = 0xBB67AE85u,
        Alpha = 0x3C6EF372u
    };

    // Keep in sync with resources/shaders/vfx/vfx_variance.glsl.
    inline uint32_t vfxPcgHash(uint32_t value)
    {
        uint32_t state = value * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    inline float vfxVarianceUnit(uint32_t seed, VarianceStream stream)
    {
        const uint32_t hash = vfxPcgHash(seed ^ static_cast<uint32_t>(stream));
        return static_cast<float>(hash) * (1.0f / 4294967295.0f);
    }

    inline float vfxVarianceSigned(uint32_t seed, VarianceStream stream)
    {
        return vfxVarianceUnit(seed, stream) * 2.0f - 1.0f;
    }
}
