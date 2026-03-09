#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::gi
{
    enum class GIQuality : uint8_t
    {
        Off = 0,
        Medium,   // 1 cascade
        High,     // 3 cascades
        Ultra     // 4 cascades + higher density
    };

    struct GISettings
    {
        bool enabled = false;
        GIQuality quality = GIQuality::Off;

        // Radiance cascade parameters
        float probeSpacing = 2.0f;            // Base spacing in meters (cascade 0)
        float cascadeMultiplier = 2.0f;       // Spacing multiplier per cascade
        uint32_t probeRaysPerUpdate = 64;     // Rays traced per probe per frame
        float temporalBlendFactor = 0.95f;    // Hysteresis for temporal accumulation
        float probeUpdateRate = 0.25f;        // Fraction of probes updated per frame
        float irradianceGamma = 5.0f;         // Encoding gamma for SH
        float maxProbeDistance = 200.0f;      // Max ray distance

        // Debug
        bool showProbes = false;
        bool showCascadeBounds = false;
        bool showProbeValidity = false;

        static GISettings fromQuality(GIQuality quality)
        {
            GISettings s;
            s.enabled = quality != GIQuality::Off;
            s.quality = quality;

            switch (quality)
            {
            case GIQuality::Off:
                break;
            case GIQuality::Medium:
                s.probeSpacing = 4.0f;
                s.probeRaysPerUpdate = 64;
                break;
            case GIQuality::High:
                s.probeSpacing = 2.0f;
                s.probeRaysPerUpdate = 128;
                break;
            case GIQuality::Ultra:
                s.probeSpacing = 2.0f;
                s.probeRaysPerUpdate = 256;
                break;
            }

            return s;
        }
    };

    struct RadianceCascadeConfig
    {
        uint32_t cascadeCount = 4;
        glm::ivec3 gridDimensions{8, 4, 8};  // Probes per cascade (x, y, z)
        float baseSpacing = 2.0f;             // Meters between probes at cascade 0
        float cascadeMultiplier = 2.0f;       // Spacing doubles per cascade
    };

    struct CascadeLevel
    {
        glm::vec3 gridOrigin{0.0f};          // World-space origin of this cascade grid
        float spacing = 2.0f;                 // Probe spacing for this cascade
        uint32_t probeCount = 0;              // Total probes in this cascade
        uint32_t probeOffset = 0;             // Offset into global probe buffer
        uint32_t updateCursor = 0;            // Which probe to update next (cycling)
    };

    // SH coefficients for irradiance (L0 + L1 = 4 coefficients, RGB = 12 floats)
    struct alignas(16) ProbeData
    {
        glm::vec4 shCoeffs[3];  // 3 x vec4 = L0.r,L1x.r,L1y.r,L1z.r / same for g,b
        glm::vec4 validity;     // x=weight(0-1), y=age, z=hitBackface%, w=reserved
    };
    static_assert(sizeof(ProbeData) == 64, "ProbeData must be 64 bytes");

    // GPU-side cascade info
    struct alignas(16) GPUCascadeInfo
    {
        glm::vec4 gridOriginSpacing;     // xyz = origin, w = spacing
        glm::ivec4 gridDimsOffset;       // xyz = grid dimensions, w = probeOffset
    };
    static_assert(sizeof(GPUCascadeInfo) == 32, "GPUCascadeInfo must be 32 bytes");

    // Push constants for GI compute shaders
    struct GIComputePushConstants
    {
        uint32_t cascadeIndex;
        uint32_t probeStartIndex;
        uint32_t probeCount;
        uint32_t raysPerProbe;
        float maxDistance;
        float temporalBlend;
        float frameRandom;
        uint32_t frameIndex;
    };

    // Stats for editor display
    struct GIDebugStats
    {
        uint32_t totalProbes = 0;
        uint32_t activeCascades = 0;
        uint32_t probesUpdatedThisFrame = 0;
        float gpuTimeMs = 0.0f;
        float averageProbeValidity = 0.0f;
    };
}
