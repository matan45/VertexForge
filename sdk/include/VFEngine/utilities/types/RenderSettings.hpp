#pragma once
#include <cstdint>
#include "../postprocess/PostProcessTypes.hpp"
#include "../../graphics/render/gi/GITypes.hpp"
#include "../atmosphere/AtmosphereSettings.hpp"
#include "../cloud/CloudSettings.hpp"
#include "../vfx/VFXScalability.hpp"

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

    // Swapchain present mode. Fifo = VSync on (no tearing, capped to refresh).
    // Mailbox = low-latency triple buffering (no tearing). Immediate = uncapped (may tear).
    enum class PresentMode : uint8_t
    {
        Fifo = 0,
        Mailbox,
        Immediate
    };

    // Multisample anti-aliasing sample count for the forward scene pass.
    enum class MsaaSamples : uint8_t
    {
        Off = 0, // 1x (no MSAA)
        X2,
        X4,
        X8
    };

    // Display-level options applied at swapchain/render-target init (restart-scoped).
    struct DisplaySettings
    {
        PresentMode presentMode = PresentMode::Mailbox;
        MsaaSamples msaa = MsaaSamples::Off;
    };

    // VK-1430: one shared resolution scale for the directional/spot/point RT shadow trace+denoise.
    // Full (default) is a byte-identical bypass of today's behavior. Half traces+denoises at half
    // resolution then runs an edge-aware joint-bilateral upsample back to full resolution.
    enum class ShadowResolutionScale : uint32_t
    {
        Full = 0,
        Half = 1
    };

    struct RTShadowSettings
    {
        // Off by default: the directional Virtual Shadow Map clipmap is the unified base
        // shadow technique (works on all GPUs, covers the full streamed range). RT is an
        // optional near-field sharpness boost layered on top via the rtShadowActive flag.
        bool enabled = false;

        // Ray parameters
        float maxRayDistance = 500.0f;
        float normalBias = 0.05f;
        float rayTMin = 0.01f;

        // Denoiser temporal
        float temporalBlend = 0.9f;
        float depthThreshold = 0.01f;
        float normalThreshold = 0.9f;

        // Denoiser spatial
        float spatialPhiDepth = 0.005f;
        float spatialPhiNormal = 32.0f;
        int spatialPasses = 3;

        // Adaptive budget
        bool adaptiveBudgetEnabled = true;
        float budgetMs = 2.0f;
        float asMemoryBudgetMB = 256.0f;

        // VK-1175: optional RT override for spot lights, layered on the spot VSM base. OFF by
        // default; reuses the ray/denoiser tunables above. spotBudget caps how many of the
        // closest/brightest shadow-casting spot lights get RT (the rest stay on VSM).
        bool spotEnabled = false;
        uint32_t spotBudget = 8;

        // VK-1176: optional RT override for point lights, layered on the point VSM base. OFF by
        // default; reuses the ray/denoiser tunables above. pointBudget caps how many of the
        // closest/brightest shadow-casting point lights get RT (the rest stay on VSM).
        bool pointEnabled = false;
        uint32_t pointBudget = 8;

        // VK-1430: shared resolution scale for the directional + spot + point RT trace/denoise.
        // Full = byte-identical to legacy behavior. Half halves the trace/denoise resolution and
        // reconstructs a full-res mask via an edge-aware joint-bilateral upsample.
        ShadowResolutionScale shadowResolutionScale = ShadowResolutionScale::Full;

        // Half = 0.5x trace/denoise dimensions, Full = 1.0x (no scaling).
        float scaleFactor() const
        {
            return shadowResolutionScale == ShadowResolutionScale::Half ? 0.5f : 1.0f;
        }
    };

    struct RTShadowStats
    {
        // GPU timing (EMA-smoothed, milliseconds)
        float rayDispatchMs = 0.0f;
        float denoiserMs = 0.0f;
        float totalRTShadowMs = 0.0f;
        float blasBuildMs = 0.0f;
        float tlasBuildMs = 0.0f;

        // Acceleration structure memory
        uint64_t blasTotalBytes = 0;
        uint64_t tlasTotalBytes = 0;
        uint64_t scratchPeakBytes = 0;
        uint32_t blasCount = 0;
        uint32_t tlasInstanceCount = 0;
        bool asMemoryOverBudget = false;

        // Adaptive budget state
        float budgetMs = 2.0f;
        float currentMaxRayDistance = 500.0f;
        int currentSpatialPasses = 3;
        bool isThrottled = false;
        uint32_t framesOverBudget = 0;
        uint32_t framesUnderBudget = 0;
    };

    struct ShadowSettings
    {
        bool enabled = true;
        ShadowQuality quality = ShadowQuality::High;

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

        // VSM resolution (spot/point virtual map pages)
        uint32_t spotResolution = 1024;
        uint32_t pointResolution = 512;

        // Directional Virtual Shadow Map clipmap
        // levelCount: number of concentric camera-centered shells (each 2x the extent).
        // baseExtent: half-size of the finest (level 0) shell in world units — smaller =
        //   sharper near shadows but more levels needed to reach the horizon.
        // depthRange: light-direction span each shell covers (scene height + view distance).
        uint32_t clipmapLevelCount = 4;
        float clipmapBaseExtent = 32.0f;
        float clipmapDepthRange = 4000.0f;

        // VK-1479 B1: page-binned directional shadow cull. When on, the directional VSM clipmap
        // pages are drawn from a per-page GPU cull (frustum + LOD + distance per level, off-screen
        // casters recovered) instead of replaying the main camera's occlusion-culled draw list once
        // per page. Default OFF — opt-in, requires GPU bring-up; the legacy path stays the fallback.
        bool perViewCulling = false;
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
        uint32_t renderLayer = 0; // VK-1415: layer bit tested against a camera's cullingMask (RTT views)
        // Terrain as a shadow CASTER (VSM page raster). Receiving shadows is unaffected.
        // Off is a large GPU win on flat maps where terrain self-shadowing is negligible.
        bool castShadows = true;
        // Opt-in sampling of terrain-layer normal and emission textures. When RVT is enabled,
        // changing this also rebuilds the terrain RVT layout to carry the extra detail planes.
        bool detailMaps = false;
    };

    struct WaterSettings
    {
        uint32_t renderLayer = 0; // VK-1415: layer bit tested against a camera's cullingMask (RTT views)
    };

    // VK-1209 — virtual texturing. Two clients over one page-table substrate:
    // terrain Runtime Virtual Texture (RVT, bakes the splat composite into a page
    // atlas) and streamed material textures (SVT, disk-paged BC7). Both default OFF.
    // Pool byte budgets are restart-scoped (Vulkan images don't resize); the
    // per-frame page budget and eviction age apply live.
    struct VirtualTextureSettings
    {
        bool rvtEnabled = false;          // terrain runtime virtual texture
        bool svtEnabled = false;          // streamed material virtual textures
        uint32_t rvtPoolBudgetMB = 128;   // terrain RVT atlas budget (restart to apply)
        // VK-1480: a hardware-safe BC7 atlas caps at 256 MiB per pool (VT_MAX_POOL_DIM); with two
        // pools (sRGB + Unorm) the 256 default splits 128/128 and the tail-only fallback path means
        // SVT now saves VRAM instead of adding it. Restart-scoped (Vulkan images don't resize).
        uint32_t svtPoolBudgetMB = 256;   // material SVT atlas budget, total across pools (restart)
        float rvtTexelsPerMeter = 8.0f;   // terrain RVT mip-0 texel density
        uint32_t pagesPerFrame = 32;      // per-frame page bake/stream budget (live)
        uint32_t evictionAgeFrames = 60;  // frames unused before a page may evict (live)
        // VK-1480: also page linear (Unorm) material maps — normal/ORM/height — through a second
        // BC7-Unorm atlas. Off = only sRGB (albedo/emission) is paged and linear maps stay plain
        // bindless (today's behavior); a named GPU sign-off item for normal-map quality. Restart.
        bool svtPageLinearMaps = true;
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

    // VK-1531: adaptive dynamic-resolution governor. Off by default. When enabled, a GPU-frame-time
    // feedback controller (DynamicResolutionBudget) steps the pre-upscale render scale within
    // [minScale, 1.0] to hold gpuFrameTimeTargetMs. 16.6 ms == a 60 FPS budget.
    struct DynamicResolutionSettings
    {
        bool enabled = false;
        float gpuFrameTimeTargetMs = 16.6f;
        float minScale = 0.5f;
    };

    struct RenderSettings
    {
        RenderPreset activePreset = RenderPreset::High;

        // VK-1453 (Phase 4) — global VFX quality tier that drives per-asset
        // scalability profiles. Derived from the preset in fromPreset(); High is the
        // neutral default so unset/Custom presets leave VFX unchanged.
        vfx::VFXQualityTier vfxQualityTier = vfx::VFXQualityTier::High;

        DisplaySettings display;
        DynamicResolutionSettings dynamicResolution;
        ShadowSettings shadows;
        RTShadowSettings rtShadows;
        LightStreamingSettings lightStreaming;
        CullingSettings culling;
        DistanceCullingSettings distanceCulling;
        TransparencySettings transparency;
        TerrainSettings terrain;
        WaterSettings water;
        VirtualTextureSettings virtualTexture;
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
                s.shadows.softShadows = false;
                s.shadows.shadowIntensity = 0.4f;
                s.rtShadows.enabled = false;
                s.rtShadows.budgetMs = 1.0f;
                s.culling.lodCrossfadeEnabled = false;
                s.culling.globalLodBias = 2.0f;
                s.distanceCulling.enabled = true;
                s.distanceCulling.staticMeshDistance = 500.0f;
                s.distanceCulling.foliageDistance = 200.0f;
                s.distanceCulling.vfxDistance = 150.0f;
                s.vfxQualityTier = vfx::VFXQualityTier::Low;
                s.terrain.lodBias = 0.5f;
                s.terrain.errorThreshold = 5.0f;
                s.virtualTexture.rvtPoolBudgetMB = 64;
                s.virtualTexture.svtPoolBudgetMB = 256;
                s.virtualTexture.rvtTexelsPerMeter = 4.0f;
                s.gi = render::gi::GISettings::fromQuality(render::gi::GIQuality::Off);
                s.vfxLOD.lod0Distance = 25.0f;
                s.vfxLOD.lod1Distance = 50.0f;
                s.vfxLOD.lod2Distance = 100.0f;
                s.animationLOD.lod0Distance = 15.0f;
                break;

            case RenderPreset::Medium:
                s.shadows.quality = ShadowQuality::Medium;
                s.shadows.softShadows = false;
                s.shadows.shadowIntensity = 0.5f;
                s.rtShadows.spatialPasses = 2;
                s.rtShadows.budgetMs = 1.5f;
                s.culling.lodCrossfadeEnabled = false;
                s.culling.globalLodBias = 1.0f;
                s.distanceCulling.enabled = true;
                s.distanceCulling.staticMeshDistance = 750.0f;
                s.distanceCulling.foliageDistance = 350.0f;
                s.distanceCulling.vfxDistance = 200.0f;
                s.vfxQualityTier = vfx::VFXQualityTier::Medium;
                s.terrain.lodBias = 0.8f;
                s.terrain.errorThreshold = 3.0f;
                s.virtualTexture.rvtPoolBudgetMB = 96;
                s.virtualTexture.rvtTexelsPerMeter = 6.0f;
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
                s.shadows.softShadows = true;
                s.shadows.shadowIntensity = 0.6f;
                s.rtShadows.spatialPasses = 4;
                s.rtShadows.maxRayDistance = 1000.0f;
                s.rtShadows.budgetMs = 4.0f;
                s.culling.lodCrossfadeEnabled = true;
                s.culling.globalLodBias = -1.0f;
                s.distanceCulling.enabled = false;
                s.distanceCulling.staticMeshDistance = 2000.0f;
                s.vfxQualityTier = vfx::VFXQualityTier::Ultra;
                s.terrain.lodBias = 1.5f;
                s.terrain.errorThreshold = 1.0f;
                s.virtualTexture.rvtPoolBudgetMB = 256;
                s.virtualTexture.svtPoolBudgetMB = 1024;
                s.virtualTexture.rvtTexelsPerMeter = 16.0f;
                s.virtualTexture.pagesPerFrame = 48;
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
