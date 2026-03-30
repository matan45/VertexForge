#pragma once
#include <cstdint>
#include "../postprocess/PostProcessTypes.hpp"
#include "../../graphics/render/gi/GITypes.hpp"
#include "../atmosphere/AtmosphereSettings.hpp"
#include "../cloud/CloudSettings.hpp"

namespace types
{
    enum class RenderPreset : uint8_t
    {
        Low = 0,
        Medium,
        High,
        Ultra,
        Custom
    };

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

    enum class DirectionalShadowMode : uint8_t
    {
        CSM = 0,
        Clipmap
    };

    enum class ShadowDebugMode : uint8_t
    {
        None = 0,
        CascadeOverlay,
        TilePoolHeatmap,
        BiasVisualization
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
        float globalLightSize = 1.0f;
        float searchRadiusMultiplier = 1.0f;

        // 0.0 = lighter shadows, 1.0 = darker shadows
        float shadowIntensity = 0.5f;

        // VSM resolution (directional virtual map pages)
        uint32_t directionalResolution = 2048;
        uint32_t spotResolution = 1024;
        uint32_t pointResolution = 512;

        // Directional shadow mode
        DirectionalShadowMode directionalMode = DirectionalShadowMode::CSM;
        uint8_t clipmapLevelCount = 16;
        float clipmapBaseExtent = 2.0f; // meters, level 0 half-extent

        // Debug visualization
        ShadowDebugMode debugMode = ShadowDebugMode::None;
    };

    struct CullingSettings
    {
        bool frustumCullingEnabled = true;
        bool occlusionCullingEnabled = true;
        bool lodSelectionEnabled = true;
        bool meshletFrustumCullingEnabled = true;
        bool meshletBackfaceCullingEnabled = true;
        bool meshletOcclusionCullingEnabled = true;

        bool terrainFrustumCullingEnabled = true;
        bool terrainMeshletCullingEnabled = true;
        bool lodCrossfadeEnabled = true;
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
        RenderPreset activePreset = RenderPreset::High;

        ShadowSettings shadows;
        LightStreamingSettings lightStreaming;
        CullingSettings culling;
        DistanceCullingSettings distanceCulling;
        TransparencySettings transparency;
        TerrainSettings terrain;
        postprocess::PostProcessSettings postProcess;
        VFXLODSettings vfxLOD;
        AnimationLODSettings animationLOD;
        render::gi::GISettings gi;
        render::atmosphere::AtmosphereSettings atmosphere;
        render::cloud::CloudSettings cloud;

        static RenderSettings createDefault()
        {
            return RenderSettings{};
        }

        static RenderSettings fromPreset(RenderPreset preset)
        {
            RenderSettings s;
            s.activePreset = preset;

            switch (preset)
            {
            case RenderPreset::Low:
                s.shadows.quality = ShadowQuality::Low;
                s.shadows.cascadeCount = 2;
                s.shadows.softShadows = false;
                s.shadows.shadowIntensity = 0.4f;
                s.culling.lodCrossfadeEnabled = false;
                s.culling.globalLodBias = 2.0f;
                s.distanceCulling.enabled = true;
                s.distanceCulling.staticMeshDistance = 500.0f;
                s.distanceCulling.foliageDistance = 200.0f;
                s.distanceCulling.vfxDistance = 150.0f;
                s.terrain.lodBias = 0.5f;
                s.terrain.errorThreshold = 5.0f;
                s.gi = render::gi::GISettings::fromQuality(render::gi::GIQuality::Off);
                s.vfxLOD.lod0Distance = 25.0f;
                s.vfxLOD.lod1Distance = 50.0f;
                s.vfxLOD.lod2Distance = 100.0f;
                s.animationLOD.lod0Distance = 15.0f;
                break;

            case RenderPreset::Medium:
                s.shadows.quality = ShadowQuality::Medium;
                s.shadows.cascadeCount = 3;
                s.shadows.softShadows = false;
                s.shadows.shadowIntensity = 0.5f;
                s.culling.lodCrossfadeEnabled = false;
                s.culling.globalLodBias = 1.0f;
                s.distanceCulling.enabled = true;
                s.distanceCulling.staticMeshDistance = 750.0f;
                s.distanceCulling.foliageDistance = 350.0f;
                s.distanceCulling.vfxDistance = 200.0f;
                s.terrain.lodBias = 0.8f;
                s.terrain.errorThreshold = 3.0f;
                s.gi = render::gi::GISettings::fromQuality(render::gi::GIQuality::Off);
                s.vfxLOD.lod0Distance = 40.0f;
                s.vfxLOD.lod1Distance = 75.0f;
                s.vfxLOD.lod2Distance = 150.0f;
                s.animationLOD.lod0Distance = 20.0f;
                break;

            case RenderPreset::High:
                // High is the default — struct defaults already match
                s.gi = render::gi::GISettings::fromQuality(render::gi::GIQuality::Medium);
                break;

            case RenderPreset::Ultra:
                s.shadows.quality = ShadowQuality::Ultra;
                s.shadows.cascadeCount = 4;
                s.shadows.softShadows = true;
                s.shadows.shadowIntensity = 0.6f;
                s.culling.lodCrossfadeEnabled = true;
                s.culling.globalLodBias = -1.0f;
                s.distanceCulling.enabled = false;
                s.distanceCulling.staticMeshDistance = 2000.0f;
                s.terrain.lodBias = 1.5f;
                s.terrain.errorThreshold = 1.0f;
                s.gi = render::gi::GISettings::fromQuality(render::gi::GIQuality::High);
                s.vfxLOD.lod0Distance = 75.0f;
                s.vfxLOD.lod1Distance = 150.0f;
                s.vfxLOD.lod2Distance = 300.0f;
                s.animationLOD.lod0Distance = 40.0f;
                break;

            case RenderPreset::Custom:
                break;
            }

            return s;
        }
    };
}
