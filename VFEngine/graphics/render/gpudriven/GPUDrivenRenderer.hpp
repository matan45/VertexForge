#pragma once

#include "GPUDrivenTypes.hpp"
// VK-1443: the 8 private subsystem-state structs live here (render::gpudriven::detail).
// Including it also transitively provides the terrain/water/vegetation/billboard manager
// and GPU-type headers those structs use by value, so the main class still sees them.
#include "GPUDrivenState.hpp"
#include "scene/MergedMeshBuffer.hpp"
#include "scene/GPUObjectStreamManager.hpp"
#include "scene/IndirectBatchManager.hpp"
#include "scene/BindlessTextureManager.hpp"
#include "scene/GPUCullLODPipeline.hpp"
#include "GPUDrivenCameraBuffer.hpp"
#include "scene/MeshShaderPipeline.hpp"
#include "scene/MeshletBuffer.hpp"
#include "scene/TextureStreamManager.hpp"
#include "scene/BoneMatrixManager.hpp"
#include "scene/ShadowPageBinner.hpp"
#include "../lighting/GPULightBufferManager.hpp"
#include "../lighting/ClusterGridManager.hpp"
#include "../lighting/LightCullingPipeline.hpp"
#include "../shadow/ShadowSystem.hpp"
#include "../volumetric/VolumetricPipeline.hpp"
#include "../lighting/LightStreamManager.hpp"
#include "../gi/GITypes.hpp"
#include "../gi/RadianceCascadeManager.hpp"
#include "../gi/GIDebugRenderer.hpp"
#include "../../core/Texture.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <utility>

namespace core
{
    class Device;
    class SwapChain;
    class DeferredDeletionQueue;
}

namespace types
{
    struct RTShadowSettings;
    struct RTShadowStats;
    struct VirtualTextureSettings;
}

namespace material
{
    struct MaterialData;
}

namespace render::mesh
{
    class MaterialTextureCache;
    class MeshStreamManager;
    struct MeshRenderData;
}

namespace render::occlusion
{
    class HiZBuffer;
    class DepthPrepass;
    class DepthPrepassPipeline;
    // VK-1443: held only as unique_ptr member; full type pulled into the .cpp family.
    class LightOcclusionCulling;
}

namespace render::volumetric
{
    // VK-1443: held only as unique_ptr member; full type pulled into the .cpp family.
    class FogVolumeBufferManager;
}

namespace render::gi
{
    // VK-1443: held only as unique_ptr members; full types pulled into the .cpp family.
    class ProbeTracePipeline;
    class ProbeUpdatePipeline;
}

namespace render::raytracing
{
    // VK-1443: held only as unique_ptr members / pointer getters / reference params;
    // full types pulled into the .cpp family (esp. GPUDrivenRenderer.cpp where the
    // GPUDrivenRenderer destructor instantiates each unique_ptr member's deleter).
    class AccelerationStructureManager;
    class RTShadowPipeline;
    class RTShadowDenoiser;
    class RTShadowProfiler;
    class RTLayeredShadowPipeline;
    class RTLayeredShadowDenoiser;
    class RTShadowUpsamplePipeline;
    class RTLayeredShadowUpsamplePipeline;
    struct RTLayeredDispatchInfo;
}

namespace terrain
{
    class TerrainTile;
}

namespace services
{
    struct OceanVisualSettings;
}

namespace water
{
    class WaterTileGrid;
    class ShoreDepthField;
}

namespace vegetation
{
    struct WindConfig;
}

namespace render::vegetation
{
    class GrassMeshShaderPipeline;
    class WindSystem;
}

namespace render::custom
{
    class PluginTextureManager;
}

namespace render::gpudriven
{
    class TerrainRVTManager; // VK-1209
    class TerrainRVTBaker;   // VK-1209
    class SVTManager;        // VK-1209 (material SVT)
    class ToonProfileGpuTable; // VK-1493 (toon profile table)
    class SelectionMaskPipeline; // VK-1490 editor selection outline

    class GPUDrivenRenderer
    {
    private:
        // VK-1443: the 8 per-subsystem state structs moved to GPUDrivenState.hpp
        // (render::gpudriven::detail). Members below reference them as detail::*.

        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<MergedMeshBuffer> mergedBuffer;
        std::unique_ptr<IndirectBatchManager> batchManager;
        std::unique_ptr<BindlessTextureManager> bindlessTextures;
        std::unique_ptr<GPUCullLODPipeline> cullPipeline;
        std::unique_ptr<GPUDrivenCameraBuffer> cameraBuffer;
        // VK-1398: swapchain image index of the submission currently being recorded. Set at the top of
        // dispatchCompute/dispatchGraphicsCompute (and by the RTT path), consumed when binding the
        // ringed RT shadow mask set (set 13) so each submission binds/writes its own per-image slot.
        uint32_t currentImageIndex = 0;
        std::unique_ptr<MeshShaderPipeline> meshShaderPipeline;
        std::unique_ptr<MeshShaderPipeline> transparentMeshShaderPipeline;
        std::unique_ptr<MeshShaderPipeline> wboitMeshShaderPipeline;
        std::unique_ptr<MeshletBuffer> meshletBuffer;
        std::unique_ptr<BoneMatrixManager> boneMatrixManager;
        std::unique_ptr<ShadowPageBinner> shadowPageBinner; // VK-1479 B1 (null until shadow init)
        std::unique_ptr<lighting::GPULightBufferManager> lightBufferManager;
        std::unique_ptr<lighting::ClusterGridManager> clusterGridManager;
        std::unique_ptr<lighting::LightCullingPipeline> lightCullingPipeline;
        std::unique_ptr<shadow::ShadowSystem> shadowSystem;
        std::unique_ptr<occlusion::LightOcclusionCulling> lightOcclusionCulling;

        // Depth prepass for meshlet-level Hi-Z occlusion culling
        std::unique_ptr<occlusion::DepthPrepass> depthPrepass;
        std::unique_ptr<occlusion::DepthPrepassPipeline> depthPrepassPipeline;

        // Editor selection bit ring + same-pass packed visibility image.
        std::unique_ptr<SelectionMaskPipeline> selectionMaskPipeline;
        std::unique_ptr<occlusion::HiZBuffer> prepassHiZ;
        uint32_t prepassHiZMipLevels = 0;
        // VK-1397: tracks whether the prepass is currently producing DLSS-D Ray
        // Reconstruction albedo guides, so a change in RR state rebuilds the prepass.
        bool prepassAlbedoActive = false;
        std::unique_ptr<volumetric::VolumetricPipeline> volumetricPipeline;
        std::unique_ptr<volumetric::FogVolumeBufferManager> fogVolumeBufferManager;
        ::postprocess::VolumetricFogSettings cachedVolumetricSettings;

        // Light streaming
        std::unique_ptr<lighting::LightStreamManager> lightStreamManager;

        // Object streaming
        std::unique_ptr<gpudriven::GPUObjectStreamManager> objectStreamManager;
        bool objectStreamingEnabled = false;

        // Global Illumination
        std::unique_ptr<gi::RadianceCascadeManager> giCascadeManager;
        std::unique_ptr<gi::ProbeTracePipeline> giTracePipeline;
        std::unique_ptr<gi::ProbeUpdatePipeline> giUpdatePipeline;
        std::unique_ptr<gi::GIDebugRenderer> giDebugRenderer;
        std::unique_ptr<raytracing::AccelerationStructureManager> accelStructManager;
        std::unique_ptr<raytracing::RTShadowPipeline> rtShadowPipeline;
        std::unique_ptr<raytracing::RTShadowDenoiser> rtShadowDenoiser;
        std::unique_ptr<raytracing::RTShadowProfiler> rtShadowProfiler;
        bool rtShadowEnabled = true;
        // Optional RT override for spot lights (VK-1175). Mirrors the directional pipeline but
        // writes one mask slice per budgeted spot light; OFF by default.
        std::unique_ptr<raytracing::RTLayeredShadowPipeline> rtSpotShadowPipeline;
        std::unique_ptr<raytracing::RTLayeredShadowDenoiser> rtSpotShadowDenoiser;
        bool rtSpotShadowEnabled = false;
        uint32_t rtSpotShadowBudget = 8;
        std::unique_ptr<raytracing::RTLayeredShadowPipeline> rtPointShadowPipeline;
        std::unique_ptr<raytracing::RTLayeredShadowDenoiser> rtPointShadowDenoiser;
        bool rtPointShadowEnabled = false;
        uint32_t rtPointShadowBudget = 8;
        // VK-1430: shared half-resolution toggle for directional+spot+point RT shadows. When true the
        // trace+denoise run at half resolution and these upsample pipelines reconstruct full-res masks
        // (lazily created on first Half frame; torn down when switched back to Full so Full mode keeps
        // zero extra allocations / dispatches). currentRTShadowHalfRes tracks the last applied state so
        // dispatch/resize know which dims to size the trace+denoiser to.
        bool rtShadowHalfResolution = false;
        std::unique_ptr<raytracing::RTShadowUpsamplePipeline> rtShadowUpsample;
        std::unique_ptr<raytracing::RTLayeredShadowUpsamplePipeline> rtSpotShadowUpsample;
        std::unique_ptr<raytracing::RTLayeredShadowUpsamplePipeline> rtPointShadowUpsample;
        // Half = ((w+1)/2, (h+1)/2); Full = (w, h). Single source of truth for the trace/denoise dims.
        std::pair<uint32_t, uint32_t> rtShadowTraceDims(uint32_t fullW, uint32_t fullH) const
        {
            return rtShadowHalfResolution
                ? std::pair<uint32_t, uint32_t>{(fullW + 1) / 2, (fullH + 1) / 2}
                : std::pair<uint32_t, uint32_t>{fullW, fullH};
        }
        // VK-1430: edge-stopping thresholds for the upsample, threaded from RTShadowSettings in
        // applyRTShadowSettings (depthThreshold reuses the denoiser depth edge-stop; normalExp reuses
        // spatialPhiNormal, the spatial denoiser's normal pow exponent).
        float rtShadowUpsampleDepthThreshold = 0.01f;
        float rtShadowUpsampleNormalExp = 32.0f;
        gi::GISettings cachedGISettings;
        bool giProbeBuffersNeedInit = true;

        std::unique_ptr<mesh::MeshStreamManager> meshStreamManager;
        bool meshStreamingEnabled = true;

        // Texture mip streaming
        std::unique_ptr<TextureStreamManager> textureStreamManager;

        bool initialized = false;
        bool enabled = false;
        bool meshShaderSupported = false;
        uint32_t hiZMipLevels = 0;
        GPUDrivenStats stats{};

        vk::DescriptorSetLayout cachedIBLLayout;
        // VK-1577: whether the mesh/terrain shaders are compiled with the reflection-probe path.
        // Probe resources ride the shared set-0 IBL layout, so this changes only the SHADER
        // permutation, never the descriptor layout — but it still requires a pipeline recreate.
        bool reflectionProbesEnabled = false;
        // Forces updateFormats() past its early-out when only the permutation changed.
        bool pipelinePermutationDirty = false;
        // Dynamic rendering formats (Vulkan 1.3) - replaces cached render passes
        std::vector<vk::Format> cachedColorFormats;
        vk::Format cachedDepthFormat = vk::Format::eUndefined;
        std::vector<vk::Format> cachedWBOITColorFormats;
        vk::Format cachedWBOITDepthFormat = vk::Format::eUndefined;

        // Plugin world-space mask (owned by RenderPassHandler's PluginTextureManager)
        custom::PluginTextureManager* pluginTextureManager = nullptr;
        uint64_t lastWorldMaskVersion = 0;

        // Entity world-mask layout when a mask has been bound, else null — every
        // MeshPipelineInitInfo construction passes this so recreates keep the mask.
        vk::DescriptorSetLayout currentWorldMaskLayout() const;
        void recreateScenePipelinesForWorldMask();
        // VK-1209: rebuild the scene mesh pipelines so set-1 SVT bindings + SVT_ENABLED match the
        // mesh pipelines' svtSampleEnabled flag (runtime SVT toggle). Creates/tears down SVTManager.
        void applySVTToggle();
        void recreateScenePipelinesForSVT(); // rebuild scene mesh pipelines with current SVT flag
        // Create the SVT manager (if absent), register its BC7 atlas in the bindless heap, and point
        // the mesh pipelines' set-1 SVT bindings at it. Safe to call repeatedly.
        void ensureSVTManager();
        // Point every SVT-enabled mesh pipeline's set-1 bindings (3/4/5) at the manager's current
        // buffers. Also called after the image-info SSBO grows (finding #2) to rebind the new handle.
        void wireSVTPipelines();

        // VK-1493: bind the toon profile table (set-1 binding 6) on every mesh pipeline. Written
        // once after pipeline (re)creation — the table's buffer handle is lifetime-stable.
        void wireToonProfilePipelines();
        void wireWindPipelines();  // VK-1580: bind the global wind UBO (set-1 binding 7) on the mesh pipelines

        detail::TerrainState terrain;

        // VK-1209 — cached virtual-texturing settings (applied via applyVirtualTextureSettings;
        // consumed by the terrain RVT manager once terrain subsystems initialize). Plain fields
        // avoid pulling the heavy types/RenderSettings.hpp into this header.
        struct VTCache
        {
            bool rvtEnabled = false;
            bool svtEnabled = false;
            uint32_t rvtPoolBudgetMB = 128;
            uint32_t svtPoolBudgetMB = 256; // VK-1480: 256 total, split across sRGB + Unorm pools
            float rvtTexelsPerMeter = 8.0f;
            uint32_t pagesPerFrame = 32;
            uint32_t evictionAgeFrames = 60;
            bool svtPageLinearMaps = true;  // VK-1480: page linear (Unorm) maps too (2nd atlas)
        } vtCache;

        // VK-1209 terrain RVT (created lazily in updateTerrain once bounds are known and
        // vtCache.rvtEnabled). Null = inactive; all frame hooks below no-op.
        std::unique_ptr<TerrainRVTManager> terrainRVT;
        std::unique_ptr<TerrainRVTBaker> terrainRVTBaker;
        glm::vec2 rvtWorldMin{0.0f};
        glm::vec2 rvtWorldMax{0.0f};
        uint32_t rvtFrameCounter = 0;
        bool rvtInvalidateAll = false; // set on terrain material change; re-bakes all fine pages

        // VK-1209 material SVT (created lazily when svtEnabled). Null = inactive.
        std::unique_ptr<SVTManager> svtManager;
        uint32_t svtFrameCounter = 0;

        // VK-1539 per-asset VRAM attribution throttle (see publishVramAttribution).
        static constexpr uint32_t kVramAttributionInterval = 16;
        uint32_t vramAttributionFrame = 0;
        uint64_t vramAttributionGeneration = 0;

        // VK-1493 toon profile GPU table (set-1 binding 6). Created with the mesh pipelines,
        // always present so binding 6 has a live buffer. Null only before init / after teardown.
        std::unique_ptr<ToonProfileGpuTable> toonProfileTable;
        // path -> SVT-tagged index (SVT_TAG_BIT | imageId) for textures opted into SVT; the texture
        // resolver returns this instead of the plain bindless index so the mesh shader pages them.
        std::unordered_map<std::string, uint32_t> svtTaggedIndices;

        detail::WaterState water;
        detail::VegetationState vegetation;
        detail::BillboardState billboard;
        detail::LightCullingState lightCulling;
        detail::MaterialState materials;
        detail::CullingConfig culling;
        detail::CachedCamera cachedCamera;
        bool wireframeMode = false;

        // VK-1334: the active cull descriptor set and the terrain view-projection override are
        // held in thread-local storage (see GPUDrivenRendererScene.cpp) so concurrent recording
        // of the main pass and one or more RTT pre-passes cannot stomp on each other. Access via
        // getThreadLocalCullDescriptorSet() / tryGetThreadLocalTerrainViewProjection().

    public:
        explicit GPUDrivenRenderer(core::Device& device, core::SwapChain& swapChain);
        ~GPUDrivenRenderer();

        GPUDrivenRenderer(const GPUDrivenRenderer&) = delete;
        GPUDrivenRenderer& operator=(const GPUDrivenRenderer&) = delete;

        void init(vk::DescriptorSetLayout iblDescriptorSetLayout,
                 const std::vector<vk::Format>& colorFormats, vk::Format depthFormat,
                 vk::ImageView sceneDepthView = nullptr);

        void cleanup();

        void setDefaultTexture(vk::ImageView view, vk::Sampler sampler);

        void updateScene(
            const std::vector<mesh::MeshRenderData>& opaqueObjects,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            float nearPlane,
            float farPlane,
            float time = 0.0f
        );

        // VK-1334: RTT recording binds per-RTT camera buffers/descriptor sets so the shared
        // mesh-pipeline CameraUBO and shared GPUDrivenCameraBuffer are never CPU-mutated during
        // a pre-pass.
        struct RTTRenderContext
        {
            GPUDrivenCameraBuffer* cameraBuffer = nullptr;   // per-RTT GPU-cull camera buffer
            vk::DescriptorSet cullDescriptorSet = nullptr;   // per-RTT cull descriptor (binding 1 -> cameraBuffer)
        };

        // Enter RTT recording scope: writes RTT camera params into ctx.cameraBuffer, swaps the
        // renderer's active cull descriptor to ctx.cullDescriptorSet, and pushes the RTT view-
        // projection into the terrain pipeline. Must be balanced by endRTTContext().
        void beginRTTContext(const RTTRenderContext& ctx, const RTTCameraParams& params);

        // Leave RTT recording scope: clears the active cull descriptor override and restores the
        // terrain pipeline's view-projection to the cached main camera value.
        void endRTTContext();

        // Allocate a per-RTT cull descriptor set from `externalPool` whose binding-1 (camera)
        // references `externalCameraBuffer`; bindings 0/2-6 mirror the renderer's current main
        // cull descriptor and are kept in sync automatically when those buffers/hiZ change.
        vk::DescriptorSet allocateRTTCullDescriptorSet(vk::DescriptorPool externalPool,
                                                       vk::Buffer externalCameraBuffer);

        // Untrack a previously allocated per-RTT cull descriptor set. Caller is responsible for
        // freeing the descriptor via its owning pool.
        void releaseRTTCullDescriptorSet(vk::DescriptorSet rttSet);

        vk::DescriptorSetLayout getCullDescriptorSetLayout() const;

        // VK-1334: thread-local RTT state accessors. Recording sites read these to pick the
        // correct cull descriptor / terrain view-projection for the current recording thread,
        // letting main and RTT record concurrently without stomping on each other.
        static vk::DescriptorSet getThreadLocalCullDescriptorSet();
        static bool tryGetThreadLocalTerrainViewProjection(glm::mat4& outVP);
        static uint32_t getThreadLocalRTTCullingMask();
        // VK-1604: true while an RTT / reflection-probe view is being recorded on this thread.
        // Distinct from the culling mask, whose "all layers" default is ambiguous.
        static bool isThreadLocalRTTContext();

        void dispatchCompute(vk::CommandBuffer cmd, uint32_t imageIndex = 0);

        // Split compute dispatch for async compute queue support
        // Records uploads, light occlusion, object culling, shadows, volumetric fog on graphics queue
        void dispatchGraphicsCompute(vk::CommandBuffer cmd, uint32_t imageIndex = 0);

        // A7: build/update BLAS+TLAS for RT shadows/GI. Extracted so BOTH the async graphics-compute
        // path AND the sync dispatchCompute path build the acceleration structures — previously only
        // the async path did, so RT shadows were silently dead whenever async compute was off.
        // Pure code motion; must be called after the transfer->compute barrier.
        void buildAccelerationStructures(vk::CommandBuffer cmd, uint32_t imageIndex);
        // Records light culling, grass compute, GI probe update on async compute queue
        void dispatchAsyncCompute(vk::CommandBuffer asyncCmd);

        void renderDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                        uint32_t screenWidth = 0, uint32_t screenHeight = 0,
                        bool selectionCoverage = false);
        void renderTransparentDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                   uint32_t screenWidth = 0, uint32_t screenHeight = 0,
                                   bool selectionCoverage = false);
        void renderWBOITDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                             uint32_t screenWidth = 0, uint32_t screenHeight = 0,
                             bool selectionCoverage = false);
        void renderBlendDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                             uint32_t screenWidth = 0, uint32_t screenHeight = 0,
                             bool selectionCoverage = false);

        void renderGIDebug(vk::CommandBuffer cmd, const glm::mat4& viewProjection);

        void initWBOITPipeline(const std::vector<vk::Format>& wboitColorFormats, vk::Format wboitDepthFormat);
        bool isWBOITReady() const { return wboitMeshShaderPipeline != nullptr && wboitMeshShaderPipeline->getPipeline(); }
        bool hasTransparentObjects() const { return mergedBuffer && mergedBuffer->getTransparentObjectCount() > 0; }

        void setEnabled(bool enabled) { this->enabled = enabled; }
        bool isEnabled() const { return enabled; }

        // VK-1490: editor selection outline — entt ids of the selected entities.
        // MergedMeshBuffer resolves them to GPU object slots while rebuilding
        // the object list; the regular scene shader records their visibility.
        // Edit-mode only by construction: the
        // play-mode persistent-slot path never records selection slots.
        void setSelectedEntities(std::unordered_set<uint32_t> entityIds)
        {
            if (mergedBuffer) mergedBuffer->setSelectedEntities(std::move(entityIds));
        }
        bool hasSelectedObjects() const
        {
            return mergedBuffer && !mergedBuffer->getSelectedObjectSlots().empty();
        }

        void setFrustumCullingEnabled(bool enabled) { culling.frustumCullingEnabled = enabled; }
        bool isFrustumCullingEnabled() const { return culling.frustumCullingEnabled; }

        void setLODSelectionEnabled(bool enabled) { culling.lodSelectionEnabled = enabled; }
        bool isLODSelectionEnabled() const { return culling.lodSelectionEnabled; }

        void setLODCrossfadeEnabled(bool enabled) { culling.lodCrossfadeEnabled = enabled; }
        bool isLODCrossfadeEnabled() const { return culling.lodCrossfadeEnabled; }

        void setOcclusionCullingEnabled(bool enabled) { culling.occlusionCullingEnabled = enabled; }
        bool isOcclusionCullingEnabled() const { return culling.occlusionCullingEnabled; }

        // VK-1479 B1: page-binned directional shadow cull (default OFF).
        void setShadowCullEnabled(bool enabled) { if (shadowSystem) shadowSystem->setShadowCullEnabled(enabled); }
        bool isShadowCullEnabled() const { return shadowSystem && shadowSystem->isShadowCullEnabled(); }

        void setMeshletFrustumCullingEnabled(bool enabled) { culling.meshletFrustumCullingEnabled = enabled; }
        bool isMeshletFrustumCullingEnabled() const { return culling.meshletFrustumCullingEnabled; }
        void setMeshletBackfaceCullingEnabled(bool enabled) { culling.meshletBackfaceCullingEnabled = enabled; }
        bool isMeshletBackfaceCullingEnabled() const { return culling.meshletBackfaceCullingEnabled; }
        void setMeshletOcclusionCullingEnabled(bool enabled) { culling.meshletOcclusionCullingEnabled = enabled; }
        bool isMeshletOcclusionCullingEnabled() const { return culling.meshletOcclusionCullingEnabled; }

        void initDepthPrepass();
        void renderDepthPrepass(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);
        void generatePrepassHiZ(vk::CommandBuffer cmd);

        // Ensures the same-pass selection visibility resources exist for import.
        bool ensureSelectionMaskResources();
        SelectionMaskPipeline* getSelectionMaskPipeline() const { return selectionMaskPipeline.get(); }
        void initAccelerationStructures();
        void ensureAccelerationStructureManager();
        // True if the device's maxBoundDescriptorSets can fit the requested set count.
        // Warns once if not; gates RT spot/point shadows on low-spec GPUs (sets 15/16).
        bool hasBoundDescriptorSetCapacity(uint32_t requiredSetCount) const;
        void dispatchRTShadow(vk::CommandBuffer cmd, uint32_t imageIndex);
        bool isRTShadowReady() const;
        void dispatchRTSpotShadow(vk::CommandBuffer cmd, uint32_t imageIndex);
        bool isRTSpotShadowReady() const;
        // Active spot RT mask (set 15) layout/descriptor — denoised variant when up, else raw,
        // else null. Used to preserve set 15 across unrelated pipeline recreates.
        vk::DescriptorSetLayout getActiveRTSpotShadowMaskLayout() const;
        vk::DescriptorSet getActiveRTSpotShadowMaskDescriptorSet() const;
        void dispatchRTPointShadow(vk::CommandBuffer cmd, uint32_t imageIndex);
        bool isRTPointShadowReady() const;
        // Active point RT mask (set 16) layout/descriptor — denoised variant when up, else raw,
        // else null. Used to preserve set 16 across unrelated pipeline recreates.
        vk::DescriptorSetLayout getActiveRTPointShadowMaskLayout() const;
        vk::DescriptorSet getActiveRTPointShadowMaskDescriptorSet() const;

        // VK-1430: shared Half-mode upsample of the per-slice layered RT shadow masks (spot/point).
        // Transitions depth/normal to SHADER_READ, runs the layered upsample over the scheduled
        // slices (reading the denoiser's half-res per-slice views), then restores depth/normal to
        // attachment layout. Mirrors the directional upsample step in dispatchRTShadow.
        void upsampleLayeredRTShadow(vk::CommandBuffer cmd,
                                     raytracing::RTLayeredShadowUpsamplePipeline& upsample,
                                     const raytracing::RTLayeredShadowDenoiser& denoiser,
                                     const std::vector<raytracing::RTLayeredDispatchInfo>& slices,
                                     uint32_t traceW, uint32_t traceH,
                                     uint32_t fullW, uint32_t fullH,
                                     uint32_t frameIndex);

        // Plugin world-space mask: lazily recreates the scene + terrain pipelines with
        // WORLD_MASK_ENABLED on the first bind, then keeps descriptors in sync.
        void setPluginTextureManager(custom::PluginTextureManager* manager) { pluginTextureManager = manager; }
        void dispatchWorldMask();
        raytracing::RTShadowPipeline* getRTShadowPipeline() const { return rtShadowPipeline.get(); }
        // Active RT shadow mask (set 13) layout/descriptor for fragment-shader
        // sampling — denoised variant when the denoiser is up, else the raw
        // mask, else null while the RT shadow pipeline isn't online. Consumed
        // by lit plugin custom pipelines (CustomPipelineManager).
        vk::DescriptorSetLayout getActiveRTShadowMaskLayout() const;
        vk::DescriptorSet getActiveRTShadowMaskDescriptorSet() const;
        void applyRTShadowSettings(const types::RTShadowSettings& settings);
        types::RTShadowStats getRTShadowStats() const;

        void setDistanceCullingEnabled(bool enabled) { culling.distanceCullingEnabled = enabled; }
        bool isDistanceCullingEnabled() const { return culling.distanceCullingEnabled; }
        void setCategoryDistance(uint32_t category, float distance) { if (category < services::CullingCategory::Count) culling.categoryDistances[category] = distance; }
        void setShadowDistanceMultiplier(float mult) { culling.shadowDistanceMultiplier = mult; }
        void setGlobalLodBias(float bias) { culling.globalLodBias = bias; }
        float getGlobalLodBias() const { return culling.globalLodBias; }

        void setTerrainFrustumCullingEnabled(bool enabled);
        void setTerrainMeshletCullingEnabled(bool enabled);

        void setViewMode(uint32_t mode) { culling.currentViewMode = mode; }
        uint32_t getViewMode() const { return culling.currentViewMode; }

        void updateHiZPyramid(vk::ImageView hiZView, vk::Sampler hiZSampler, uint32_t mipLevels);

        const GPUDrivenStats& getStats() const { return stats; }

        void updateStatsFromGPU();

        MeshletCullingStats getMeshletCullingStats();

        void setMaterialTextureCache(mesh::MaterialTextureCache* cache) { materials.textureCache = cache; }

        uint32_t getHiZMipLevels() const { return hiZMipLevels; }

        std::pair<vk::ImageView, vk::Sampler> getPrepassHiZViewSampler() const;
        vk::ImageView getPrepassNormalImageView() const;
        vk::Image getPrepassNormalImage() const;
        // DLSS-D Ray Reconstruction albedo guides (VK-1397). Valid only while Ray
        // Reconstruction is active; null otherwise.
        vk::Image getPrepassDiffuseAlbedoImage() const;
        vk::ImageView getPrepassDiffuseAlbedoImageView() const;
        vk::Image getPrepassSpecularAlbedoImage() const;
        vk::ImageView getPrepassSpecularAlbedoImageView() const;

        void updateFormats(const std::vector<vk::Format>& colorFormats, vk::Format depthFormat,
                          vk::DescriptorSetLayout newIBLLayout = nullptr);

        // VK-1577: toggle the reflection-probe shader permutation. A no-op unless the value actually
        // changes, so it is safe to call every frame; when it does change it recreates the mesh,
        // transparent, WBOIT and terrain pipelines exactly once.
        void setReflectionProbesEnabled(bool enabled);
        [[nodiscard]] bool areReflectionProbesEnabled() const { return reflectionProbesEnabled; }

        uint32_t getMergedVertexCount() const;
        uint32_t getMergedIndexCount() const;
        uint32_t getRegisteredMeshCount() const;
        uint32_t getRegisteredTextureCount() const;

        uint32_t getBatchCount() const;
        uint32_t getCommandsPerBatch() const;
        uint32_t getTotalCapacity() const;
        uint64_t getDrawCommandBufferSize() const;
        uint64_t getDrawCountBufferSize() const;
        uint64_t getPerDrawDataBufferSize() const;
        uint64_t getTotalMemoryUsage() const;

        lighting::GPULightBufferManager* getLightBufferManager() const { return lightBufferManager.get(); }
        GPUDrivenCameraBuffer* getCameraBuffer() const { return cameraBuffer.get(); }
        lighting::ClusterGridManager* getClusterGridManager() const { return clusterGridManager.get(); }
        lighting::LightCullingPipeline* getLightCullingPipeline() const { return lightCullingPipeline.get(); }
        shadow::ShadowSystem* getShadowSystem() const { return shadowSystem.get(); }

        void setDeletionQueue(core::DeferredDeletionQueue* queue);

        void setWireframeMode(bool enabled);

        // Light streaming
        lighting::LightStreamManager* getLightStreamManager() const { return lightStreamManager.get(); }
        void initLightStreaming(const lighting::LightStreamingConfig& config = {});

        // Object streaming
        gpudriven::GPUObjectStreamManager* getObjectStreamManager() const { return objectStreamManager.get(); }
        void initObjectStreaming(const gpudriven::ObjectStreamConfig& config = {});
        void setObjectStreamingEnabled(bool enabled) { objectStreamingEnabled = enabled; }
        bool isObjectStreamingEnabled() const { return objectStreamingEnabled; }

        // Global Illumination
        void initGI(const gi::GISettings& settings);
        void cleanupGI();
        void applyGISettings(const gi::GISettings& settings);
        gi::RadianceCascadeManager* getGICascadeManager() const { return giCascadeManager.get(); }
        gi::GIDebugRenderer* getGIDebugRenderer() const { return giDebugRenderer.get(); }
        const gi::GISettings& getGISettings() const { return cachedGISettings; }

        // Texture streaming
        TextureStreamManager* getTextureStreamManager() const { return textureStreamManager.get(); }
        const TextureStreamStats* getTextureStreamStats() const;

        // VK-1418: scene-wide bindless table, used by the RTT adapter to reserve/repoint per-image
        // RTT material slots and by the per-frame RTT material inject pass.
        gpudriven::BindlessTextureManager* getBindlessTextureManager() const { return bindlessTextures.get(); }

        void setVisibleLightsFromBVH(const std::vector<uint32_t>& visibleLights);
        void clearVisibleLights();
        bool isBVHLightCullingEnabled() const { return lightCulling.useBVH; }

        void initLightOcclusionCulling(occlusion::HiZBuffer* hiZBuffer);
        bool isLightOcclusionCullingEnabled() const { return lightCulling.useOcclusion; }

        uint32_t getTotalSceneLights() const;
        uint32_t getLightsAfterBVHCull() const;
        uint32_t getLightsAfterHiZCull() const;

        void readBackLightOcclusionResults();

        void initVolumetricFog(::postprocess::VolumetricQuality quality);
        void setVolumetricFogEnabled(bool enabled);
        bool isVolumetricFogEnabled() const;
        volumetric::VolumetricPipeline* getVolumetricPipeline() const { return volumetricPipeline.get(); }
        void updateVolumetricSettings(const ::postprocess::VolumetricFogSettings& settings);

        void updateTerrain(const std::vector<terrain::TerrainTile*>& visibleTiles,
                           const glm::vec3& cameraPosition,
                           const std::string& terrainMaterialPath = "",
                           const glm::vec2& terrainGridWorldMin = glm::vec2(0.0f),
                           const glm::vec2& terrainGridWorldMax = glm::vec2(0.0f));
        void renderTerrainDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                               uint32_t screenWidth = 0, uint32_t screenHeight = 0);
        void clearTerrainData();
        void evictTerrainTile(int32_t coordX, int32_t coordZ);
        void setSelectedTerrainTile(int32_t coordX, int32_t coordZ);
        void clearSelectedTerrainTile();

        void invalidateTerrainLayerData() { terrain.layerDataDirty = true; }

        void setTerrainRenderingEnabled(bool enabled) { terrain.renderingEnabled = enabled; }
        bool isTerrainRenderingEnabled() const { return terrain.renderingEnabled; }
        void setTerrainLODBias(float bias) { terrain.lodBias = bias; }
        void setTerrainErrorThreshold(float threshold) { terrain.errorThreshold = threshold; }
        void setTerrainTextureScale(float scale) { terrain.textureScale = scale; }
        void setTerrainRenderLayer(uint32_t layer) { terrain.renderLayer = layer; }
        // Out-of-line: a change invalidates all cached VSM pages (they were baked
        // with the old caster set).
        void setTerrainCastShadows(bool cast);
        // VK-1610: does the project ALLOW per-layer normal/emission detail maps. Whether they are
        // actually compiled in is derived from the loaded terrain material (a material with no
        // normal/emission maps gets nothing from the permutation but would still pay the
        // four-plane RVT pool), so this re-resolves the terrain rather than flipping a switch.
        // Safe to call before init.
        void setTerrainDetailMaps(bool allowed);

        // VK-1209 — apply virtual-texturing settings (RVT/SVT enable, pool budgets,
        // page-per-frame + eviction age). Pool byte budgets are restart-scoped; the live
        // knobs (pagesPerFrame, evictionAge, enable toggles) forward to the managers.
        void applyVirtualTextureSettings(const types::VirtualTextureSettings& settings);
        bool isTerrainRVTEnabled() const { return vtCache.rvtEnabled; }

        // VK-1209 terrain RVT frame hooks (all no-op unless RVT is active). Frame order:
        // updateTerrainRVTResidency (frame start, CPU) -> bakeTerrainRVT (before scene pass) ->
        // [terrain draws, samples atlas + writes feedback] -> copyTerrainRVTFeedback (after pass).
        void updateTerrainRVTResidency();
        void bakeTerrainRVT(vk::CommandBuffer cmd);
        void copyTerrainRVTFeedback(vk::CommandBuffer cmd);
        bool isTerrainRVTActive() const;

        // VK-1209 material SVT frame hooks (no-op unless SVT active). Order: beginSVTFrame (frame
        // start) -> updateAndUploadSVT (before scene pass: residency + disk extract + upload) ->
        // [meshes sample the atlas + write feedback] -> copySVTFeedback (after scene pass).
        void beginSVTFrame();
        void updateAndUploadSVT(vk::CommandBuffer cmd);
        void copySVTFeedback(vk::CommandBuffer cmd);
        bool isSVTActive() const;

        // VK-1493: record the toon profile table upload (staging copy + barrier) if a
        // profile changed. Cheap (12 KB); called before the scene pass, next to the SVT
        // upload. No-op unless a profile row is dirty.
        void uploadToonProfiles(vk::CommandBuffer cmd);
        // VK-1607: `waterBodies` are bounded lakes/pools that contribute their own tiles alongside
        // the ocean's, and `oceanActive` says whether to emit the ocean tile grid at all - a scene
        // whose only water is a lake still gets here, it just has no ocean to tile.
        void updateWater(const services::OceanVisualSettings& visualSettings,
                         float baseWaterHeight,
                         const glm::vec3& cameraPosition,
                         float oceanPatchSize,
                         bool worldMode = false,
                         const ::water::WaterTileGrid* tileGrid = nullptr,
                         const ::water::ShoreDepthField* shoreField = nullptr,
                         const std::vector<::water::WaterBodyDesc>& waterBodies = {},
                         bool oceanActive = true);

        // VK-1605: record the shore-depth texture upload and latch the frame time that drives
        // camera.u_Time (so CPU buoyancy stays in phase with the drawn breakers). MUST be called
        // outside a render pass; it sits next to dispatchOceanFFT. The copy is a no-op unless the
        // field's version changed.
        void uploadShoreDepthField(vk::CommandBuffer cmd, float time);

        // VK-1606: hand the frame's water impulses (script calls, auto-wakes, wake emitters) to the
        // ripple sim, then record its fixed sub-steps. queueWaterImpulses is called from
        // updateGPUDrivenSceneData; dispatchWaterRipples MUST be called outside a render pass and
        // sits next to dispatchOceanFFT / uploadShoreDepthField.
        void queueWaterImpulses(const std::vector<::water::WaterImpulse>& impulses);
        void dispatchWaterRipples(vk::CommandBuffer cmd, float time);

        void renderWaterDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);
        void clearWaterData();

        void setWaterRenderingEnabled(bool enabled) { water.renderingEnabled = enabled; }
        bool isWaterRenderingEnabled() const { return water.renderingEnabled; }
        void setWaterRenderLayer(uint32_t layer) { water.renderLayer = layer; }

        void copySceneColorForRefraction(vk::CommandBuffer cmd, vk::Image colorImage, uint32_t width, uint32_t height);
        render::water::WaterRefractionResources* getRefractionResources() { return water.refractionResources.get(); }
        render::water::WaterCausticsResources* getCausticsResources() { return water.causticsResources.get(); }
        void recreateRefractionResources(vk::ImageView sceneDepthView);

        void initOceanFFT(const std::array<render::water::OceanFFTConfig, 3>& bandConfigs,
                          const std::array<bool, 3>& bandEnabled);
        void cleanupOceanFFT();
        void setOceanEnabled(bool enabled);
        bool isOceanEnabled() const { return water.oceanEnabled; }
        void updateOceanConfig(const std::array<render::water::OceanFFTConfig, 3>& bandConfigs,
                               const std::array<bool, 3>& bandEnabled);
        void dispatchOceanFFT(vk::CommandBuffer cmd, float time);
        void readbackOceanDisplacement();
        float getOceanHeightAt(const glm::vec2& worldXZ) const;

        // VK-1607: which per-tile simulation LOD this world XZ falls on, from the tile layout the
        // last updateWater published. Public so it can be exercised directly; getOceanHeightAt uses
        // it to drop exactly the bands the vertex shader dropped (see water::lodBandMask).
        [[nodiscard]] uint32_t waterTileLodAt(const glm::vec2& worldXZ) const;

        // Vegetation rendering
        void initVegetationSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                     const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);
        void renderGrassDraw(vk::CommandBuffer cmd,
                             uint32_t screenWidth = 0, uint32_t screenHeight = 0);
        void updateWind(float deltaTime, const ::vegetation::WindConfig& config);
        void updateVegetationStreaming(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                       const std::vector<terrain::TerrainTile*>& allLoadedTiles,
                                       const glm::vec3& cameraPosition);
        void setGrassRenderingEnabled(bool enabled) { vegetation.grassRenderingEnabled = enabled; }
        bool isGrassRenderingEnabled() const { return vegetation.grassRenderingEnabled; }
        void setGrassRenderConfig(const ::vegetation::GrassRenderConfig& config) { vegetation.grassConfig = config; }
        void setBillboardPalette(const std::vector<detail::VegetationState::BillboardGPUEntry>& entries) { vegetation.billboardPalette = entries; }
        void setActiveBillboardEntry(int32_t index) { vegetation.activeBillboardEntry = index; }
        uint32_t getBillboardPaletteSize() const { return static_cast<uint32_t>(vegetation.billboardPalette.size()); }
        void setBillboardPaletteLoader(std::function<std::vector<::vegetation::BillboardPaletteEntry>()> loader)
        {
            vegetation.billboardPaletteLoader = std::move(loader);
        }
        // VK-1443: body in GPUDrivenRendererVegetation.cpp (resolves billboard texture
        // paths through the bindless texture stream manager).
        void setBillboardPaletteFromEntries(const std::vector<::vegetation::BillboardPaletteEntry>& entries);
        void addVegetationTile(int32_t coordX, int32_t coordZ);
        void removeVegetationTile(int32_t coordX, int32_t coordZ);
        void clearVegetationData();
        void markVegetationTileDirty(int32_t coordX, int32_t coordZ);
        void dispatchGrassCompute(vk::CommandBuffer cmd, const std::vector<terrain::TerrainTile*>& visibleTiles);
        void autoLoadBillboardPaletteFromECS();
        bool needsVegetationUpload(const std::vector<terrain::TerrainTile*>& tiles) const;
        std::vector<vegetation::GrassInstanceGPU> collectBillboardInstances(const std::vector<terrain::TerrainTile*>& tiles);
        void ensureInstanceStagingCapacity(vk::DeviceSize requiredSize);
        void cleanupVegetation();

        // Billboard rendering
        void updateBillboards(const std::vector<BillboardInstanceGPU>& instances);
        // Texture-resolving overload: texturePaths[i] is the .vfImage/texture path for
        // instances[i] (empty = default texture). The bindless index is resolved here
        // (where the bindless texture manager lives) and written into each instance
        // before upload. Paths are resolved idempotently, so calling per-frame is cheap.
        void updateBillboards(std::vector<BillboardInstanceGPU> instances,
                              const std::vector<std::string>& texturePaths);
        void renderBillboardDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                  float time = 0.0f,
                                  uint32_t screenWidth = 0, uint32_t screenHeight = 0);
        void clearBillboardData();
        void setBillboardRenderingEnabled(bool enabled) { billboard.renderingEnabled = enabled; }
        bool isBillboardRenderingEnabled() const { return billboard.renderingEnabled; }
        const BillboardRenderStats& getBillboardStats() const { return billboard.stats; }

        void setBrushOverlay(const glm::vec3& worldPos, float worldRadius, float falloff, float shape, float stampRotation = 0.0f);
        void setStampOverlay(vk::Buffer buffer, uint32_t width, uint32_t height, float rotation);
        void clearStampOverlay();

        void setTileDataLoader(TerrainStreamManager::TileDataLoader loader);
        void releaseMeshAsset(const std::string& meshPath);
        void releaseTextureAsset(const std::string& texturePath);
        void releaseMaterialAsset(const std::string& materialPath);

        void setTileRAMEvictor(TerrainStreamManager::TileRAMEvictor evictor);
        void setTileLoadContextProvider(TerrainStreamManager::TileLoadContextProvider loader);

        float getTerrainUpdateUs() const { return terrain.updateUs; }
        float getTerrainStreamingUs() const { return terrain.streamingUs; }
        float getTerrainBuildTileDataUs() const { return terrain.buildTileDataUs; }
        float getTerrainUploadTileDataUs() const { return terrain.uploadTileDataUs; }

        float getWaterReadbackUs() const { return water.readbackUs; }
        float getWaterDispatchUs() const { return water.dispatchUs; }
        float getWaterUpdateUs() const { return water.updateUs; }
        float getWaterRenderUs() const { return water.renderUs; }
        const TerrainStreamingStats* getTerrainStreamingStats() const;
        TerrainCullingStats getTerrainCullingStats();

    private:
        bool registerMaterialTextures(const std::string& materialPath);
        void registerTerrainLayerTextures(const std::string& materialPath);
        void registerTextureDependencies(const std::string& materialPath,
                                          const std::vector<std::string>& texturePaths);

        void updateMeshStreaming(const std::vector<mesh::MeshRenderData>& opaqueObjects,
                                 const glm::vec3& cameraPosition);
        // VK-1539: assemble + publish per-asset VRAM attribution from the streaming managers for
        // the editor Memory Diagnostics window. Called once per frame from updateScene; gated by
        // GpuAllocationStats::diagnosticsActive and throttled to kVramAttributionInterval frames.
        void publishVramAttribution();
        void registerSceneMaterialTextures(const std::vector<mesh::MeshRenderData>& opaqueObjects);
        TextureIndexResolver createTextureResolver();
        BoneOffsetResolver updateAnimationBones();
        void patchBoneOffsetsInGPUData(const std::vector<BoneDefragResult>& moves);
        // VK-1418: per-frame inject of live RTT bindless slots into bound entities' material
        // texture indices (albedo/emission). Runs before uploadDirtyObjects each frame.
        void patchRenderTextureMaterialSlots(uint32_t imageIndex);
        void updateClusterGrid(const glm::mat4& projection, float nearPlane, float farPlane);
        void updatePipelineDescriptors();
        void updateAllPipelinesHiZ();
        std::pair<vk::ImageView, vk::Sampler> getHiZViewSampler() const;

        void initBillboardSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                    const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);
        void initTerrainSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                   const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);
        // VK-1209: rebuild the terrain pipeline so set 5 + RVT_ENABLED match rvtSampleEnabled
        // (used when the RVT config is toggled at runtime). Gathers the same layouts as init.
        void recreateTerrainPipelineForRVT();
        // VK-1609: derive TERRAIN_HEIGHT_BLEND from the freshly resolved terrain.layerData (any
        // layer with a non-zero height-blend contrast) and, if it changed, recompile the terrain
        // pipeline AND rebuild the RVT baker so the live composite and the baked pages stay in
        // lockstep. Cheap no-op when the flag is unchanged.
        void syncTerrainHeightBlendPermutation();
        // VK-1610: same idea for TERRAIN_DETAIL_MAPS, but the flag also changes the RVT plane
        // layout (2 planes -> 4), so this rebuilds the RVT *manager* as well as the baker rather
        // than just recompiling. `effective` is `terrain.detailMapsAllowed && the resolved material
        // carries normal/emission maps`. Called from registerTerrainLayerTextures BEFORE the
        // bindless registration loop, because it decides whether that loop uploads those textures
        // at all — and, unlike setTerrainDetailMaps used to, it never re-enters that function.
        void syncTerrainDetailMapsPermutation(bool effective);
        void createGrassBuffers(uint32_t maxInstances);
        void initWaterSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                 const std::vector<vk::Format>& colorFormats, vk::Format depthFormat,
                                 vk::ImageView sceneDepthView);
        void createMultiBandOceanDescriptor();
        void updateMultiBandOceanDescriptor();
        void collectShadowVisibleLights(std::unordered_set<uint32_t>& outLights, bool& outHasFilter);
        void buildAndDispatchLightOcclusion(vk::CommandBuffer cmd);
        void recordShadowPasses(vk::CommandBuffer cmd, bool hasMeshObjects, bool hasTerrainTiles);
        void updateLightCullingState(vk::CommandBuffer cmd);
        // VK-1479 B1: page-binned shadow cull, split around the transfer->compute barrier so it
        // rides the existing barrier. prepare = advance ring, stage per-level data + pageBinBase,
        // reset counts, upload (before the barrier); dispatch = cull + post-barrier (after the main
        // cull). Both gated on active bin pages + not an RTT pass, and use the SAME objectCount +
        // activeIndices as the main cull (the anti-desync guarantee).
        void prepareShadowBinCull(vk::CommandBuffer cmd);
        void dispatchShadowBinCull(vk::CommandBuffer cmd);
        [[nodiscard]] bool shadowBinCullReady() const;
        // A1: fill the shadow system's reused dynamic-caster-bounds buffer from resolved GPU
        // object data (world matrix + local AABB + flags) so it marks only overlapped pages.
        void gatherDynamicShadowCasterBounds();
        void dispatchVolumetricFog(vk::CommandBuffer cmd);
        void dispatchGIProbeUpdate(vk::CommandBuffer cmd);

        static uint64_t makeTileKey(int32_t x, int32_t z);
    };
}
