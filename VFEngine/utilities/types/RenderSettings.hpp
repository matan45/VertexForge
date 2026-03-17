#pragma once
#include <cstdint>
#include "../postprocess/PostProcessTypes.hpp"
#include "../../graphics/render/gi/GITypes.hpp"

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

        // PCSS
        bool softShadows = true;

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
        float globalLodBias = 0.0f;
    };

    struct DistanceCullingSettings
    {
        bool enabled = false;
        float staticMeshDistance = 1000.0f;
        float terrainDistance = 2000.0f;
        float foliageDistance = 500.0f;
        float vfxDistance = 300.0f;
        float decalDistance = 200.0f;
        float billboardDistance = 1000.0f;
        float waterDistance = 2000.0f;
        float shadowDistanceMultiplier = 0.5f;
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

    struct VFXLODSettings
    {
        float lod0Distance = 50.0f;
        float lod1Distance = 100.0f;
        float lod2Distance = 200.0f;
        float transitionZone = 10.0f;
    };

    struct AnimationLODSettings
    {
        float lod0Distance = 25.0f;
        float lod1Distance = 75.0f;
        float lod2Distance = 150.0f;
        float lod3Distance = 300.0f;
        uint32_t lod0Interval = 1;
        uint32_t lod1Interval = 2;
        uint32_t lod2Interval = 6;
        uint32_t maxStreamingInitPerFrame = 4;
    };

    struct ShadowLODSettings
    {
        bool enabled = true;
        float tier0Distance = 30.0f;   // < 30m  -> 2048
        float tier1Distance = 80.0f;   // < 80m  -> 1024
        float tier2Distance = 150.0f;  // < 150m -> 512
        // > 150m -> no shadow

        uint32_t tier0Resolution = 2048;
        uint32_t tier1Resolution = 1024;
        uint32_t tier2Resolution = 512;

        // Tighter tiers for static lights (cached shadows)
        float staticTier0Distance = 20.0f;
        float staticTier1Distance = 50.0f;
        float staticTier2Distance = 100.0f;
    };

    struct LightStreamingSettings
    {
        uint32_t maxPointLights = 1024;
        uint32_t maxSpotLights = 512;
        float distanceWeight = 1.0f;
        float intensityWeight = 0.5f;
        float radiusWeight = 0.3f;
        float shadowWeight = 2.0f;
        float hysteresisMargin = 0.05f;
    };

    struct RenderSettings
    {
        ShadowSettings shadows;
        ShadowLODSettings shadowLOD;
        LightStreamingSettings lightStreaming;
        CullingSettings culling;
        DistanceCullingSettings distanceCulling;
        TransparencySettings transparency;
        TerrainSettings terrain;
        postprocess::PostProcessSettings postProcess;
        VFXLODSettings vfxLOD;
        AnimationLODSettings animationLOD;
        render::gi::GISettings gi;

        static RenderSettings createDefault()
        {
            return RenderSettings{};
        }
    };
}
