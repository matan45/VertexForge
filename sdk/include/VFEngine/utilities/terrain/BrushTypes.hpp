#pragma once

#include <cstdint>
#include <algorithm>
#include <string>
#include <glm/glm.hpp>

namespace terrain
{
    enum class BrushType : uint8_t
    {
        Raise = 0,
        Lower = 1,
        Smooth = 2,
        Flatten = 3,
        Noise = 4,
        Stamp = 5,
        Erosion = 6,
        Terrace = 7,
        Ramp = 8,
        // VK-1616: hydraulic (water-flow) erosion. Unlike 0-7 this does NOT run through
        // brush_compute.glsl's switch -- it has its own pipeline over a multi-tile region
        // (see TerrainHydraulicErosion.hpp), so like Ramp it takes a branch of its own in
        // TerrainService::applyBrush. Append only: the GLSL switch is a raw integer switch.
        Hydraulic = 9
    };

    enum class BrushFalloff : uint8_t
    {
        Constant = 0,
        Linear = 1,
        Smooth = 2,
        Sharp = 3
    };

    // VK-1613: `Square` was missing here while the Sculpt, Paint and Cave panels all offered it as
    // combo index 1 and cast that straight in. The value was never dead — every consumer implements
    // it as the `else` of `shape == Circle`, i.e. a Chebyshev (square) distance: see
    // WeightBrushApplicator / HoleBrushApplicator / CaveBrushApplicator and brush_compute.glsl,
    // plus the viewport preview overlay in mesh_terrain.glsl. So this names what already shipped
    // rather than adding behaviour, and the casts stop producing an unnamed enumerator.
    enum class BrushShape : uint8_t
    {
        Circle = 0,
        Square = 1
    };

    struct BrushParams
    {
        float radius = 5.0f;
        float strength = 10.0f;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;
        float stampRotation = 0.0f;
        float stampScale = 1.0f;
        bool stampSubtract = false;
        float talusAngle = 45.0f;
        float terraceStepHeight = 2.0f;
        float terraceSharpness = 0.5f;
        float rampWidth = 5.0f;
        float rampFalloff = 2.0f;

        // VK-1616: hydraulic erosion (Mei/Decaudin/Hu virtual pipe model). The timestep, pipe
        // constants and minimum tilt angle are deliberately NOT exposed -- dt is derived from the
        // vertex spacing and CFL-clamped in HydraulicParams::validate(), because a user-settable dt
        // is the documented way to make this solver explode. The thermal smoothing sub-pass reuses
        // `talusAngle` above rather than adding a seventh control.
        float hydraulicRainRate = 0.35f;
        float hydraulicSedimentCapacity = 1.2f;
        float hydraulicEvaporation = 0.015f;
        float hydraulicHardness = 0.5f;
        float hydraulicSmoothing = 0.2f;
        uint32_t hydraulicIterations = 24;

        std::string stampImagePath;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            strength = std::clamp(strength, 0.0f, 100.0f);
            hydraulicRainRate = std::clamp(hydraulicRainRate, 0.0f, 2.0f);
            hydraulicSedimentCapacity = std::clamp(hydraulicSedimentCapacity, 0.1f, 5.0f);
            hydraulicEvaporation = std::clamp(hydraulicEvaporation, 0.0f, 0.2f);
            hydraulicHardness = std::clamp(hydraulicHardness, 0.0f, 1.0f);
            hydraulicSmoothing = std::clamp(hydraulicSmoothing, 0.0f, 1.0f);
            hydraulicIterations = std::clamp(hydraulicIterations, 1u, 128u);
        }
    };

    struct BrushGPUParams
    {
        glm::vec2 brushCenter{0.0f};
        glm::vec2 tileWorldOrigin{0.0f};
        float brushRadius = 0.0f;
        float brushStrength = 0.0f;
        float vertexSpacing = 0.0f;
        uint32_t verticesPerSide = 0;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;
        BrushType brushType = BrushType::Raise;
        float deltaTime = 0.0f;
        float targetHeight = 0.0f;
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
        bool invert = false;
        float stampRotation = 0.0f;
        float stampScale = 1.0f;
        uint32_t stampWidth = 0;
        uint32_t stampHeight = 0;
        float talusAngle = 45.0f;
        float terraceStepHeight = 2.0f;
        float terraceSharpness = 0.5f;
    };
}
