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
        float maxProbeDistance = 200.0f;      // Max ray distance

        // Far-field GI extension for open worlds
        bool farFieldEnabled = false;
        float farFieldMaxDistance = 500.0f;   // Extended GI range in meters
        uint32_t farFieldCascadeCount = 2;    // Additional coarse cascades
        float farFieldProbeSpacing = 32.0f;   // Coarse spacing for far-field probes
        uint32_t farFieldRaysPerUpdate = 32;  // Fewer rays for far-field (cheaper)
        float farFieldUpdateRate = 0.1f;      // 10% probes updated per frame (slower)

        // Screen-Space GI (supplements probe-based GI with high-frequency local bounces)
        bool ssgiEnabled = false;
        float ssgiIntensity = 0.5f;
        float ssgiRadius = 2.0f;
        float ssgiMaxDistance = 100.0f;
        int ssgiSampleCount = 8;
        float ssgiTemporalBlend = 0.1f;
        bool ssgiHalfResolution = true;

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
                s.farFieldEnabled = true;
                s.farFieldCascadeCount = 1;
                s.farFieldMaxDistance = 500.0f;
                s.ssgiEnabled = true;
                s.ssgiSampleCount = 8;
                s.ssgiHalfResolution = true;
                break;
            case GIQuality::Ultra:
                s.probeSpacing = 2.0f;
                s.probeRaysPerUpdate = 256;
                s.farFieldEnabled = true;
                s.farFieldCascadeCount = 2;
                s.farFieldMaxDistance = 1000.0f;
                s.ssgiEnabled = true;
                s.ssgiSampleCount = 12;
                s.ssgiHalfResolution = false;
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
        glm::ivec3 gridDimensions{8, 4, 8};  // Grid dimensions for this cascade
        float spacing = 2.0f;                 // Probe spacing for this cascade
        uint32_t probeCount = 0;              // Total probes in this cascade
        uint32_t probeOffset = 0;             // Offset into global probe buffer
        uint32_t updateCursor = 0;            // Which probe to update next (cycling)
        bool isFarField = false;              // Far-field cascades use fewer rays and update slower
    };

    // SH coefficients for irradiance (L0 + L1 + L2 = 9 coefficients per channel)
    // Each channel uses 3 x vec4: [L0,L1y,L1z,L1x], [L2_-2,L2_-1,L2_0,L2_1], [L2_2,pad,pad,pad]
    struct alignas(16) ProbeData
    {
        glm::vec4 shR[3];   // Red channel: 9 SH coefficients in 3 vec4
        glm::vec4 shG[3];   // Green channel
        glm::vec4 shB[3];   // Blue channel
        glm::vec4 validity;  // x=weight(0-1), y=age, z=hitBackface%, w=reserved
    };
    static_assert(sizeof(ProbeData) == 160, "ProbeData must be 160 bytes");

    struct alignas(16) GPUCascadeInfo
    {
        glm::vec4 gridOriginSpacing;     // xyz = origin, w = spacing
        glm::ivec4 gridDimsOffset;       // xyz = grid dimensions, w = probeOffset
    };
    static_assert(sizeof(GPUCascadeInfo) == 32, "GPUCascadeInfo must be 32 bytes");

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

    struct alignas(16) SSGIParamsUBO
    {
        glm::mat4 projection;
        glm::mat4 inverseProjection;
        glm::mat4 view;
        glm::mat4 inverseView;
        glm::mat4 prevViewProjection;
        glm::vec4 params;           // radius, maxDistance, intensity, temporalBlend
        glm::vec2 resolution;
        glm::vec2 texelSize;
        float nearPlane;
        float farPlane;
        uint32_t sampleCount;
        uint32_t frameIndex;
        uint32_t historyValid;
        uint32_t halfResolution;
        float padding[2];
    };

    struct GIDebugStats
    {
        uint32_t totalProbes = 0;
        uint32_t activeCascades = 0;
        uint32_t probesUpdatedThisFrame = 0;
    };
}
