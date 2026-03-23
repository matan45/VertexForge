#pragma once
#include "../ProceduralExport.hpp"
#include "../noise/NoiseTypes.hpp"
#include <cstdint>

namespace procedural
{
    struct VF_PROCEDURAL_API HeightmapParams
    {
        uint32_t width = 1024;
        uint32_t height = 1024;
        uint32_t seed = 42;

        NoiseType noiseType = NoiseType::Perlin;
        FractalType fractalType = FractalType::FBM;

        int octaves = 6;
        float frequency = 0.005f;
        float amplitude = 1.0f;
        float lacunarity = 2.0f;
        float persistence = 0.5f;

        DomainWarpParams domainWarp;

        // Post-processing
        float heightExponent = 1.0f;    // >1 = deeper valleys/sharper peaks, <1 = flatter plateaus
        bool invert = false;            // Flip heightmap (mountains become canyons)
        bool terracing = false;         // Quantize heights into discrete steps
        int terraceSteps = 8;           // Number of terrace levels (2-64)
    };
}
