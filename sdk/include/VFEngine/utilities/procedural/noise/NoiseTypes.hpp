#pragma once
#include <cstdint>

namespace procedural
{
    enum class NoiseType : uint8_t
    {
        Perlin,
        Simplex
    };

    enum class FractalType : uint8_t
    {
        None,
        FBM,
        Ridged,
        Billowy
    };

    struct DomainWarpParams
    {
        bool enabled = false;
        float amplitude = 50.0f;
        float frequency = 0.005f;
    };
}
