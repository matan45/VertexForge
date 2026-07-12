#pragma once
#include "postprocess/PostProcessTypes.hpp"
#include "../core/OffScreen.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "material/MaterialManager.hpp"
#include "math/Frustum.hpp"
#include "terrain/TerrainHitResult.hpp"
#include "../../services/data/RenderHookTypes.hpp"
#include "../../services/data/RenderHookContext.hpp"
#include "../../services/data/CustomPipelineTypes.hpp"
#include "../../services/data/PostProcessEffectTypes.hpp"
#include "../../services/data/PluginTextureTypes.hpp"
#include "../../services/providers/render/IDecalRenderProvider.hpp"
#include "common/SharedCameraUBO.hpp"
#include "graph/RenderGraphTypes.hpp"
#include "raytracing/GPUTimestampQueryPool.hpp"
#include "gpudriven/billboard/BillboardGPUTypes.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace services
{
    class IVFXRuntimeProvider;
    class ITerrainRenderProvider;
    class IOceanRenderProvider;
    class IGrassRenderProvider;
    class IVegetationRenderProvider;
}

namespace core
{
    class Device;
    class SwapChain;
    class DeferredDeletionQueue;
    class ThreadCommandPoolManager;
}

namespace render::gpudriven
{
    class GPUDrivenRenderer;
    class TerrainRaycastPipeline;
}

namespace render::postprocess
{
    class PostProcessPipeline;
}

namespace render::transparency
{
    class WBOITPipeline;
}

namespace render::decal
{
    class DecalPipeline;
}

namespace render::custom
{
    class CustomPipelineManager;
    class PluginTextureManager;
    struct CustomLightingSets;
}

namespace render::vfx
{
    class DistortionResources;
    class VFXDistortionComposite;
}

namespace render::upscaling
{
    class MotionVectorPass;
    class ReactiveMaskPass;
    class UpscaleManager;
}

namespace render::volumetric
{
    class VolumetricFogComposite;
    class VolumetricPipeline;
}

namespace render::gi
{
    class SSGIPipeline;
}

namespace render::ssr
{
    class SSRPipeline;
}


namespace render::graph
{
    class RenderGraph;
    class RenderGraphProfiler;
}

namespace render::atmosphere
{
    class AtmospherePipeline;
    struct AtmosphereSettings;
}

namespace render::cloud
{
    class CloudPipeline;
    struct CloudSettings;
}

namespace render
{
    class ClearColor;
    class IBL;
    class DebugRenderer;

    namespace mesh
    {
        class StaticMeshPipeline;
        struct MeshRenderData;
        struct CameraFrustumRenderData;
        struct AudioSphereRenderData;
        struct PhysicsColliderRenderData;
        struct LightGizmoRenderData;
        struct ClusterDebugRenderData;
        struct UICanvasOutlineRenderData;
        struct UICanvasImageRenderData;
    }

    namespace billboard
    {
        class BillboardPipeline;
        struct BillboardRenderData;
    }

    namespace text
    {
        class TextPipeline;
        struct TextRenderData;
    }

    namespace ui
    {
        class UIRenderPipeline;
        struct UIImageRenderData;
        class UITextPipeline;
        struct UITextRenderData;
    }

    namespace selection
    {
        class SelectionOutlineComposite; // VK-1490 editor selection outline
    }

    class RenderPassHandler
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<ClearColor> clearColor;
        std::unique_ptr<IBL> iblRenderer;
        std::unique_ptr<mesh::StaticMeshPipeline> meshPipeline;
        std::unique_ptr<billboard::BillboardPipeline> billboardPipeline;
        std::unique_ptr<text::TextPipeline> textPipeline;
        std::unique_ptr<ui::UIRenderPipeline> uiPipeline;
        std::unique_ptr<ui::UITextPipeline> uiTextPipeline;
        std::unique_ptr<DebugRenderer> debugRenderer;
        std::unique_ptr<occlusion::CameraOcclusionManager> cameraOcclusionManager;
        std::unique_ptr<gpudriven::GPUDrivenRenderer> gpuDrivenRenderer;
        std::unique_ptr<gpudriven::TerrainRaycastPipeline> terrainRaycastPipeline;
        std::unique_ptr<postprocess::PostProcessPipeline> postProcessPipeline;
        std::unique_ptr<volumetric::VolumetricFogComposite> volumetricFogComposite;
        std::unique_ptr<gi::SSGIPipeline> ssgiPipeline;
        std::unique_ptr<ssr::SSRPipeline> ssrPipeline;
        std::unique_ptr<atmosphere::AtmospherePipeline> atmospherePipeline;
        std::unique_ptr<cloud::CloudPipeline> cloudPipeline;
        std::unique_ptr<transparency::WBOITPipeline> wboitPipeline;
        bool wboitEnabled = true;

        std::unique_ptr<upscaling::MotionVectorPass> motionVectorPass;
        std::unique_ptr<upscaling::ReactiveMaskPass> reactiveMaskPass;
        // Set when the opaque-only scene color was copied this frame (consumed
        // by executeUpscalePass to generate the reactive mask)
        mutable bool preTransparencyCaptured = false;
        bool upscaleFirstFrame = true;

        std::unique_ptr<decal::DecalPipeline> decalPipeline;
        bool decalRenderingEnabled = true;

        std::unique_ptr<vfx::DistortionResources> distortionResources;
        std::unique_ptr<vfx::VFXDistortionComposite> distortionComposite;
        bool distortionInitialized = false;

        core::OffscreenResources& offscreenResources;

        std::unique_ptr<common::SharedCameraUBO> sharedCameraUBO;
        float currentSnowAccumulation = 0.0f;
        float currentWetness = 0.0f;

        bool meshPipelineInitialized = false;
        std::vector<mesh::MeshRenderData> currentMeshDrawList;
        std::vector<mesh::MeshRenderData> customShaderMeshDrawList;
        std::vector<mesh::MeshRenderData> combinedMeshDrawList;
        const math::Frustum* currentFrustum = nullptr;

        bool billboardPipelineInitialized = false;
        std::vector<billboard::BillboardRenderData> currentBillboardDrawList;

        bool textPipelineInitialized = false;
        std::vector<text::TextRenderData> currentTextDrawList;

        bool uiPipelineInitialized = false;
        std::vector<ui::UIImageRenderData> currentUIImageDrawList;

        bool uiTextPipelineInitialized = false;
        std::vector<ui::UITextRenderData> currentUITextDrawList;

        bool debugRendererInitialized = false;
        glm::mat4 currentView{1.0f};
        glm::mat4 currentProjection{1.0f};
        glm::mat4 unjitteredProjection{1.0f};
        glm::mat4 prevView{1.0f};
        glm::mat4 prevProjection{1.0f};
        glm::mat4 prevPrevView{1.0f};
        glm::mat4 prevPrevProjection{1.0f};
        glm::vec2 currentJitterOffset{0.0f};
        uint32_t taaFrameIndex = 0;

        bool gpuDrivenRendererInitialized = false;
        bool asyncComputeActive = false;
        bool parallelSceneRecording = false;
        core::ThreadCommandPoolManager* sceneThreadPoolManager = nullptr;

        // VK-1490: editor selection (entt ids) for the silhouette outline passes
        std::vector<uint32_t> selectedEntityIds;

        // Scene recording stats
        mutable float lastSceneRecordingUs = 0.0f;
        mutable uint32_t lastSceneSecondaryCount = 0;
        glm::vec3 currentCameraPosition{0.0f};
        float currentNearPlane = 0.1f;
        float currentFarPlane = 1000.0f;
        float currentTime = 0.0f;

        services::IVFXRuntimeProvider* vfxRuntimeProvider = nullptr;
        services::ITerrainRenderProvider* terrainRenderProvider = nullptr;
        services::IOceanRenderProvider* oceanRenderProvider = nullptr;
        services::IGrassRenderProvider* grassRenderProvider = nullptr;

        mutable uint32_t lastOceanConfigVersion = 0;
        mutable bool oceanFFTInitialized = false;

        mutable std::unordered_map<std::string, bool> customShaderRequirementCache;
        material::CallbackId materialChangeCallbackId{};
        mutable bool lightOcclusionInitialized = false;
        bool vfxLightingInitialized = false;

        float brushOverlayRadius_ = 0.0f;
        float brushOverlayFalloff_ = 0.0f;
        float brushOverlayShape_ = 0.0f;
        float brushOverlayStampRotation_ = 0.0f;

        struct RegisteredRenderHook {
            plugin::RenderHookHandle handle;
            plugin::RenderPassHookPoint hookPoint;
            plugin::RenderHookCallback callback;
        };
        std::vector<RegisteredRenderHook> renderHooks;
        uint64_t nextRenderHookId = 1;

        // Plugin-owned custom pipelines/meshes (handle-based, all Vulkan engine-side)
        std::unique_ptr<custom::CustomPipelineManager> customPipelineManager;

        // Plugin-owned 2D textures + world-space mask binding (handle-based)
        std::unique_ptr<custom::PluginTextureManager> pluginTextureManager;

        // Additional frustums for RTT cameras — merged with main when loading terrain tiles.
        // Mutable because they are consumed (cleared) inside the const updateGPUDrivenSceneData().
        mutable std::vector<std::pair<math::Frustum, glm::vec3>> additionalTerrainFrustums;

        // Render graph
        std::unique_ptr<graph::RenderGraph> frameGraph;
        std::unique_ptr<graph::RenderGraphProfiler> graphProfiler;
        // Lazy init on first enable request from the profiler UI (GpuPassStats);
        // unsupported = timestamp queries unavailable, never retry
        bool graphProfilerInitialized = false;
        bool graphProfilerUnsupported = false;

        // VK-1480: aux GPU timestamps for work recorded inside the GPU-driven mesh pass
        // that RenderGraphProfiler can't see: the raw VT commands (RVT bake / SVT update /
        // feedback copy) plus the terrain-cost rows (GPU cull + VSM raster, depth
        // prepass + HiZ, terrain main draw). Same query-pool discipline as graphProfiler:
        // per-frame-in-flight slots, a dense written range read back a frame later, gated
        // on the profiler being enabled (GpuPassStats). Scope names must be string
        // literals (stored as const char*). Lazily init'd in syncGraphProfiler, cleaned
        // up beside graphProfiler.
        static constexpr uint32_t kVTTimestampScopes = 8;
        // Mutable: resetFrame/writeTimestamp are non-const, but the VT scopes are
        // recorded from the const draw path (like the other mutable frame-scratch state).
        mutable raytracing::GPUTimestampQueryPool vtTimestampPool;
        bool vtTimestampPoolInitialized = false;
        // Set per frame by beginVTTimestamps (profiler enabled + pool valid); read by
        // the scope brackets. Mutable: the scopes are written from const draw methods.
        mutable bool vtTimestampsActiveThisFrame = false;
        // Next free query index this frame (2 per scope, packed densely so readback
        // never touches an unwritten query when a VT path is inactive that frame).
        mutable uint32_t vtQueryCursor = 0;
        // Per frame-in-flight slot: queries written (dense, 2×scopes) and the scope
        // names in write order, so readback reads exactly the range and labels rows.
        mutable std::array<uint32_t, core::MAX_FRAMES_IN_FLIGHT> vtSlotQueryCount{};
        mutable std::array<std::vector<const char*>, core::MAX_FRAMES_IN_FLIGHT> vtSlotScopeNames{};
        graph::ResourceHandle sceneColorHandle;
        graph::ResourceHandle depthHandle;
        // VK-1490: imported per frame only while a selection outline is active
        graph::ResourceHandle selectionMaskHandle;
        std::unique_ptr<selection::SelectionOutlineComposite> selectionOutlineComposite;
        // Scoped MSAA: multisampled scene targets. Pre-resolve passes (ClearColor,
        // sky, clouds, opaque meshes) write these; the opaque pass resolves into
        // sceneColorHandle/depthHandle. Equal to the single-sample handles when MSAA
        // is off, so all write() calls stay valid unconditionally.
        graph::ResourceHandle sceneColorMSAAHandle;
        graph::ResourceHandle depthMSAAHandle;

    public:
        explicit RenderPassHandler(core::Device& device, core::SwapChain& swapChain,
                                   core::OffscreenResources& offscreenResources);
        ~RenderPassHandler();

        void init();

        void recreate();

        IBL* getIBL() const { return iblRenderer.get(); }

        mesh::StaticMeshPipeline* getMeshPipeline() const { return meshPipeline.get(); }
        bool isMeshPipelineInitialized() const { return meshPipelineInitialized; }

        void initMeshPipeline(bool enableGPUDriven = true);
        void reinitMeshPipelineWithDefaults();
        void reinitMeshPipelineWithIBL();

        void setMeshDrawList(std::vector<mesh::MeshRenderData>&& meshes);
        void setCurrentFrustum(const math::Frustum* frustum) { currentFrustum = frustum; }

        void initBillboardPipeline();
        billboard::BillboardPipeline* getBillboardPipeline() const { return billboardPipeline.get(); }
        bool isBillboardPipelineInitialized() const { return billboardPipelineInitialized; }
        void setBillboardDrawList(std::vector<billboard::BillboardRenderData>&& billboards);

        void initTextPipeline();
        text::TextPipeline* getTextPipeline() const { return textPipeline.get(); }
        bool isTextPipelineInitialized() const { return textPipelineInitialized; }
        void setTextDrawList(std::vector<text::TextRenderData>&& textEntities);
        void appendTextDrawList(std::vector<text::TextRenderData>&& textEntities);

        void registerExternalTexture(const std::string& key, uint32_t imageIndex,
                                     vk::ImageView imageView, vk::Sampler sampler);

        // Drops the entry in both UI and Billboard external-texture caches. Required when the
        // underlying vk::ImageView/vk::Sampler are about to be destroyed (RTT teardown or resize),
        // otherwise the cached descriptor sets reference freed handles.
        void unregisterExternalTexture(const std::string& key);

        void initUIRenderPipeline();
        ui::UIRenderPipeline* getUIRenderPipeline() const { return uiPipeline.get(); }
        bool isUIRenderPipelineInitialized() const { return uiPipelineInitialized; }
        void setUIImageDrawList(std::vector<ui::UIImageRenderData>&& images);

        void initUITextPipeline();
        ui::UITextPipeline* getUITextPipeline() const { return uiTextPipeline.get(); }
        bool isUITextPipelineInitialized() const { return uiTextPipelineInitialized; }
        void setUITextDrawList(std::vector<ui::UITextRenderData>&& labels);

        void initDebugRenderer();
        void setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums);
        void setAudioSphereDrawList(std::vector<mesh::AudioSphereRenderData>&& spheres);
        void setPhysicsColliderDrawList(std::vector<mesh::PhysicsColliderRenderData>&& colliders);
        void setLightGizmoDrawList(std::vector<mesh::LightGizmoRenderData>&& gizmos);
        void setShowPhysicsDebug(bool show);
        bool getShowPhysicsDebug() const;
        void setShowClusterDebug(bool show);
        bool getShowClusterDebug() const;
        void setClusterDebugData(mesh::ClusterDebugRenderData&& data);
        void setUICanvasOutlineDrawList(std::vector<mesh::UICanvasOutlineRenderData>&& outlines);
        void setUICanvasImageDrawList(std::vector<mesh::UICanvasImageRenderData>&& images);
        void setWireframeMode(bool enabled);

        // VK-1490: editor selection outline — selected entt ids pushed per frame
        // by the editor (empty clears). Forwarded to the GPU-driven renderer,
        // which resolves them to GPU object slots for the SelectionMask pass.
        void setSelectedEntityDrawList(std::vector<uint32_t>&& entityIds);
        bool hasSelectedEntities() const { return !selectedEntityIds.empty(); }

        void setShowNavmeshDebug(bool show);
        bool getShowNavmeshDebug() const;
        void updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices);
        void clearNavmeshDebugMesh();

        void setDebugCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
        void setUnjitteredProjection(const glm::mat4& proj) { unjitteredProjection = proj; }
        const glm::mat4& getUnjitteredProjection() const { return unjitteredProjection; }
        void setTAAJitterData(const glm::vec2& jitter, uint32_t frameIndex) { currentJitterOffset = jitter; taaFrameIndex = frameIndex; }
        bool isDebugRendererInitialized() const { return debugRendererInitialized; }
        DebugRenderer* getDebugRenderer() const { return debugRenderer.get(); }

        occlusion::CameraOcclusionManager* getCameraOcclusionManager() const { return cameraOcclusionManager.get(); }

        gpudriven::GPUDrivenRenderer* getGPUDrivenRenderer() const { return gpuDrivenRenderer.get(); }
        bool isGPUDrivenRendererInitialized() const { return gpuDrivenRendererInitialized; }
        void setAsyncComputeActive(bool active) { asyncComputeActive = active; }
        void setParallelSceneRecording(bool enabled, core::ThreadCommandPoolManager* poolManager = nullptr)
        {
            parallelSceneRecording = enabled;
            sceneThreadPoolManager = poolManager;
        }
        float getLastSceneRecordingUs() const { return lastSceneRecordingUs; }
        uint32_t getLastSceneSecondaryCount() const { return lastSceneSecondaryCount; }

        // Record all async-eligible compute work into the given command buffer
        // (light culling, grass, GI, atmosphere, clouds, VFX, ocean FFT, motion vectors)
        void recordAsyncCompute(vk::CommandBuffer asyncCmd, uint32_t frameIndex) const;

        void setDeletionQueue(core::DeferredDeletionQueue* queue);
        void setGPUDrivenCameraData(const glm::vec3& cameraPos, float nearPlane, float farPlane, float time = 0.0f);

        void updateSharedCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                   const glm::vec3& cameraPos, float time);
        vk::Buffer getSharedCameraBuffer() const;

        void setSnowAccumulation(float value) { currentSnowAccumulation = value; }
        void setWetness(float value) { currentWetness = value; }

        void setVisibleLightsFromBVH(const std::vector<uint32_t>& visibleLights);
        void clearVisibleLights();
        void readBackLightOcclusionResults();
        void readBackTerrainRaycastResults();

        void setRaycastCursorUV(const glm::vec2& uv);
        void clearRaycastCursor();
        terrain::TerrainHitResult getTerrainHitResult() const;

        void setBrushOverlayParams(float radius, float falloff, float shape, float stampRotation = 0.0f);
        void updateBrushOverlayFromHitResult();
        void setStampOverlay(vk::Buffer buffer, uint32_t width, uint32_t height, float rotation);
        void clearStampOverlay();

        void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider);

        void setVFXDistanceCullingEnabled(bool enabled);
        void setVFXDrawDistance(float distance);
        void setBillboardDistanceCullingEnabled(bool enabled);
        void setBillboardDrawDistance(float distance);
        void setTerrainDistanceCullingEnabled(bool enabled);
        void setTerrainDrawDistance(float distance);

        void setTerrainRenderProvider(services::ITerrainRenderProvider* provider);
        void clearTerrainData();
        void evictTerrainTile(int32_t coordX, int32_t coordZ);
        void setSelectedTerrainTile(int32_t coordX, int32_t coordZ);
        void clearSelectedTerrainTile();

        void addTerrainFrustum(const math::Frustum& frustum, const glm::vec3& cameraPos);
        void clearAdditionalTerrainFrustums();

        void setGrassRenderProvider(services::IGrassRenderProvider* provider);
        void setVegetationRenderProvider(services::IVegetationRenderProvider* provider);

        void setOceanRenderProvider(services::IOceanRenderProvider* provider);
        void clearWaterData();

        void setViewMode(uint32_t mode);
        uint32_t getViewMode() const;

        void setFrustumCullingEnabled(bool enabled);
        void setOcclusionCullingEnabled(bool enabled);
        void setLODSelectionEnabled(bool enabled);
        void setLODCrossfadeEnabled(bool enabled);
        void setMeshletFrustumCullingEnabled(bool enabled);
        void setMeshletBackfaceCullingEnabled(bool enabled);
        void setMeshletOcclusionCullingEnabled(bool enabled);
        void setGlobalLodBias(float bias);
        void setTerrainFrustumCullingEnabled(bool enabled);
        void setTerrainMeshletCullingEnabled(bool enabled);

        void setWBOITEnabled(bool enabled);

        void setTerrainRenderingEnabled(bool enabled);
        void setTerrainLODBias(float bias);
        void setTerrainErrorThreshold(float threshold);
        void setTerrainTextureScale(float scale);

        void setBillboardRenderingEnabled(bool enabled);
        // Feed GPU mesh-shader billboards (worldMarker billboards). texturePaths[i]
        // pairs with instances[i] and is resolved to a bindless index inside the
        // GPU-driven renderer. Safe to call per-frame.
        void updateBillboards(std::vector<render::gpudriven::BillboardInstanceGPU> instances,
                              const std::vector<std::string>& texturePaths);

        void setDecalRenderingEnabled(bool enabled);
        void setDecalDrawList(const std::vector<services::DecalRenderData>& decals);

        occlusion::CameraRenderData* createCamera(occlusion::CameraId id, bool enableOcclusion = true);
        void removeCamera(occlusion::CameraId id);
        void setActiveCamera(occlusion::CameraId id);
        occlusion::CameraId getActiveCameraId() const;

        void initHiZ(occlusion::CameraId cameraId, vk::Image depthImage, vk::ImageView depthView,
                     vk::Format depthFormat);

        postprocess::PostProcessPipeline* getPostProcessPipeline() const { return postProcessPipeline.get(); }
        const glm::mat4& getPrevView() const { return prevView; }
        const glm::mat4& getPrevProjection() const { return prevProjection; }
        void resetUpscaleFirstFrame() { upscaleFirstFrame = true; }

        plugin::RenderHookHandle registerRenderHook(plugin::RenderPassHookPoint hookPoint,
                                                     plugin::RenderHookCallback callback);
        void unregisterRenderHook(plugin::RenderHookHandle handle);

        // Plugin custom render pipelines — draws are enqueued per frame and recorded
        // inside the scene pass (depth-tested against scene geometry).
        plugin::CustomPipelineHandle createCustomPipeline(const plugin::CustomPipelineDesc& desc);
        plugin::CustomMeshHandle uploadCustomMesh(plugin::CustomMeshData&& data);
        void enqueueCustomDraw(plugin::CustomDrawItem&& item);
        void destroyCustomPipeline(plugin::CustomPipelineHandle handle);
        void destroyCustomMesh(plugin::CustomMeshHandle handle);

        // Plugin custom post-process effects (VK-1409) — full-screen read-modify-write
        // effects slotted into the post-process chain (ordered by priority relative
        // to the built-in tonemap). Forwarded to PostProcessPipeline.
        plugin::PostProcessEffectHandle registerPostProcessEffect(const plugin::PostProcessEffectDesc& desc);
        void updatePostProcessEffectParams(plugin::PostProcessEffectHandle handle, std::vector<std::byte>&& params);
        void setPostProcessEffectEnabled(plugin::PostProcessEffectHandle handle, bool enabled);
        void unregisterPostProcessEffect(plugin::PostProcessEffectHandle handle);

        // Plugin 2D textures + world-space mask (terrain dim / entity discard)
        plugin::PluginTextureHandle createPluginTexture2D(uint32_t width, uint32_t height,
                                                          plugin::TextureFormat format);
        void updatePluginTexture2D(plugin::PluginTextureHandle handle, std::vector<std::byte>&& data);
        void destroyPluginTexture2D(plugin::PluginTextureHandle handle);
        // VK-1488 — expose a plugin texture as a generic UI image source. register returns
        // the deterministic key ("__plugintex_<id>__") to place on UIImageComponent.externalTextureKey;
        // the per-frame repoint in draw() keeps its bindless slot pointed at the texture.
        std::string registerPluginUITexture(plugin::PluginTextureHandle handle);
        void unregisterPluginUITexture(plugin::PluginTextureHandle handle);
        void bindWorldMask(plugin::PluginTextureHandle handle,
                           const glm::vec3& worldMin, const glm::vec3& worldMax,
                           const plugin::WorldMaskParams& params);
        void unbindWorldMask();
        void setWorldMaskParams(const plugin::WorldMaskParams& params);
        float sampleWorldMask(float worldX, float worldZ) const;
        void setWorldMaskDebugEnabled(bool enabled);
        bool getWorldMaskDebugEnabled() const;
        custom::PluginTextureManager* getPluginTextureManager() const { return pluginTextureManager.get(); }

        void initVolumetricFogComposite(volumetric::VolumetricPipeline* volPipeline);
        void resetVolumetricFogComposite();
        volumetric::VolumetricFogComposite* getVolumetricFogComposite() const { return volumetricFogComposite.get(); }

        void initSSGI();
        void resetSSGI();
        gi::SSGIPipeline* getSSGIPipeline() const { return ssgiPipeline.get(); }

        void initSSR();
        void resetSSR();
        void applySSRSettings(const ::postprocess::SSRSettings& settings);
        ssr::SSRPipeline* getSSRPipeline() const { return ssrPipeline.get(); }

        void initAtmosphere();
        void resetAtmosphere();
        void applyAtmosphereSettings(const atmosphere::AtmosphereSettings& settings);
        atmosphere::AtmospherePipeline* getAtmospherePipeline() const { return atmospherePipeline.get(); }

        void initCloud();
        void resetCloud();
        void applyCloudSettings(const cloud::CloudSettings& settings);
        cloud::CloudPipeline* getCloudPipeline() const { return cloudPipeline.get(); }

        void cleanUp();

        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

    private:
        void initGPUDrivenRenderer();
        void updateGPUDrivenHiZ() const;
        bool materialRequiresCustomShader(const std::string& materialPath) const;
        static bool computeMaterialRequiresCustomShader(const std::string& materialPath);

        std::unordered_set<std::string> collectCustomShaderMaterials(
            const std::vector<mesh::MeshRenderData>& meshes) const;
        void updateDebugBoundingBoxState();
        void rebuildCombinedMeshDrawList();

        void recreateOverlayPipelines();
        void cleanUpPipelines() const;

        void updateGPUDrivenSceneData() const;
        void executeOcclusionPasses(const vk::CommandBuffer& commandBuffer) const;
        void dispatchTerrainRaycast(const vk::CommandBuffer& commandBuffer) const;
        void executePostProcess(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void executePreUpscalePostProcess(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void executePostUpscalePostProcess(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void updateSunScreenPosition() const;

        // Graph-managed dispatch variants (call *GraphManaged sub-pipeline methods)
        void drawSceneMeshesGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void drawGPUDrivenMeshPassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                               DebugRenderer* debugRendererPtr, bool hasCustomShaderMeshes, bool hasVFX) const;
        void recordParallelScenePassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                                 vk::DescriptorSet iblDescriptorSet, DebugRenderer* debugRendererPtr,
                                                 bool hasCustomShaderMeshes, bool wboitActive) const;
        void recordInlineScenePassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                               vk::DescriptorSet iblDescriptorSet, DebugRenderer* debugRendererPtr,
                                               bool hasCustomShaderMeshes, bool wboitActive) const;
        // Per-frame lighting descriptor sets for lit plugin custom pipelines
        // (returns empty sets until the gpu-driven renderer is initialized).
        custom::CustomLightingSets buildCustomLightingSets(vk::DescriptorSet iblDescriptorSet) const;
        // Once-per-frame poll: forwards the active RT shadow mask layout to the
        // custom pipeline manager (rebuilds lit pipelines when it first arrives).
        void syncCustomPipelineRTShadow();
        // Once-per-frame poll of the profiler UI's enable request (GpuPassStats):
        // lazy-inits the GPU pass profiler, reads back last frame's timestamps
        // and publishes the snapshot for the editor.
        void syncGraphProfiler(uint32_t imageIndex);

        // VK-1480: aux VT timestamp scopes (see vtTimestampPool). beginVTTimestamps
        // resets the pool + this frame's bookkeeping (call once, outside a render pass,
        // before the first VT command); a vtScopeBegin/vtScopeEnd pair brackets one raw
        // VT command scope (begin returns a handle to pass to end, UINT32_MAX when
        // inactive); endVTTimestamps records the slot's dense written range. All no-ops
        // unless the profiler is enabled and the pool is valid.
        void beginVTTimestamps(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        uint32_t vtScopeBegin(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex, const char* name) const;
        // Split begin for scopes recorded on a job thread (parallel scene recording):
        // vtScopeAlloc reserves the query pair + name on the RECORDING thread (the
        // cursor/name bookkeeping is not thread-safe), then the job writes the start
        // timestamp into its own secondary via vtScopeBeginAt. A scope that was
        // alloc'd MUST be written (begin+end) or readback stays NOT_READY all frame.
        uint32_t vtScopeAlloc(uint32_t imageIndex, const char* name) const;
        void vtScopeBeginAt(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                            uint32_t startQueryIndex) const;
        void vtScopeEnd(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex, uint32_t startQueryIndex) const;
        void endVTTimestamps(uint32_t imageIndex) const;
        void drawOverlaysGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void drawUIOverlaysGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void executeUpscaleGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

        void executeRenderHooks(plugin::RenderPassHookPoint hookPoint,
                                const vk::CommandBuffer& commandBuffer,
                                uint32_t imageIndex) const;

        void initDistortionPass();
        void executeDistortionPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

        // Copies the opaque-only scene color (pre-VFX/WBOIT) into
        // offscreenResources.preTransparencyColor for reactive mask generation.
        // No-op unless upscaling is active and its resources exist.
        void capturePreTransparencyColor(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        // Render graph
        void importFrameResources(uint32_t imageIndex);
        void buildFrameGraph(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

    public:
        graph::RenderGraphProfiler* getGraphProfiler() const { return graphProfiler.get(); }
    };
}
