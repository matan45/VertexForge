#pragma once
#include <cstdint>
#include "../postprocess/PostProcessTypes.hpp"

namespace types
{
    enum class ShadowQuality : uint8_t
    {
        Off = 0,
        Low, // 512px
        Medium, // 1024px
        High, // 2048px
        Ultra // 4096px
    };

    enum class CascadeSplitMode : uint8_t
    {
        Linear = 0,
        Logarithmic,
        Practical
    };

    enum class PCFKernelSize : uint8_t
    {
        x1 = 0, // 1x1 - Hard shadows
        x3 = 2, // 3x3
        x5 = 4 // 5x5
    };

    struct ShadowAtlasConfig
    {
        uint32_t atlasSize = 4096;
        uint32_t directionalResolution = 2048;
        uint32_t spotResolution = 1024;
        uint32_t pointResolution = 512;

        static ShadowAtlasConfig fromQuality(ShadowQuality quality)
        {
            ShadowAtlasConfig config;
            switch (quality)
            {
            case ShadowQuality::Off:
                config.atlasSize = 0;
                config.directionalResolution = 0;
                config.spotResolution = 0;
                config.pointResolution = 0;
                break;
            case ShadowQuality::Low:
                config.atlasSize = 2048;
                config.directionalResolution = 512;
                config.spotResolution = 256;
                config.pointResolution = 256;
                break;
            case ShadowQuality::Medium:
                config.atlasSize = 4096;
                config.directionalResolution = 1024;
                config.spotResolution = 512;
                config.pointResolution = 512;
                break;
            case ShadowQuality::High:
                config.atlasSize = 4096;
                config.directionalResolution = 2048;
                config.spotResolution = 1024;
                config.pointResolution = 512;
                break;
            case ShadowQuality::Ultra:
                config.atlasSize = 8192;
                config.directionalResolution = 4096;
                config.spotResolution = 2048;
                config.pointResolution = 1024;
                break;
            }
            return config;
        }
    };

    struct ShadowSettings
    {
        bool enabled = true;
        ShadowQuality quality = ShadowQuality::High;

        // CSM settings
        uint8_t cascadeCount = 4;
        CascadeSplitMode cascadeSplitMode = CascadeSplitMode::Practical;

        // Bias
        float shadowBias = 0.005f;
        float slopeBias = 1.5f;
        float normalBias = 0.02f;

        // PCF
        PCFKernelSize pcfKernelSize = PCFKernelSize::x3;
        bool softShadowsEnabled = true;

        // 0.0 = lighter shadows, 1.0 = darker shadows
        float shadowIntensity = 0.5f;

        ShadowAtlasConfig atlas;
    };

    struct CullingSettings
    {
        bool frustumCullingEnabled = true;
        bool occlusionCullingEnabled = true;
        bool lodSelectionEnabled = true;
        bool meshletFrustumCullingEnabled = true;
        bool meshletBackfaceCullingEnabled = true;

        bool terrainFrustumCullingEnabled = true;
        bool terrainMeshletCullingEnabled = true;
    };

    struct TransparencySettings
    {
        bool wboitEnabled = true;
    };

    struct TerrainSettings
    {
        bool enabled = true;
        float lodBias = 1.0f;
        float errorThreshold = 2.0f;
        float textureScale = 0.1f;
        uint32_t shadowLOD = 2; // LOD level for terrain shadows (0=highest, 3=lowest)
    };

    struct RenderSettings
    {
        ShadowSettings shadows;
        CullingSettings culling;
        TransparencySettings transparency;
        TerrainSettings terrain;
        postprocess::PostProcessSettings postProcess;

        static RenderSettings createDefault()
        {
            return RenderSettings{};
        }
    };
}
