#pragma once
#include <glm/glm.hpp>
#include "../../services/providers/render/IOffScreenProvider.hpp"
#include "../../services/data/RenderHookTypes.hpp"
#include "../render/occlusion/CameraOcclusionManager.hpp"
#include "terrain/TerrainHitResult.hpp"
#include "terrain/BrushTypes.hpp"
#include "terrain/TerrainHydraulicErosion.hpp"
#include "postprocess/PostProcessTypes.hpp"
#include "../render/gi/GITypes.hpp"
#include "atmosphere/AtmosphereSettings.hpp"
#include "cloud/CloudSettings.hpp"
#include "types/RenderSettings.hpp"
#include "../render/lighting/LightStreamManager.hpp"
#include "../render/gpudriven/scene/GPUObjectStreamTypes.hpp"
#include "../render/tools/ImmediateDebugTypes.hpp"
#include "../../services/providers/render/IDecalRenderProvider.hpp"
#include <functional>
#include <memory>
#include <string_view>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <span>
#include <entt/entt.hpp>

namespace events
{
    struct SubscriptionToken;
}

namespace core
{
    class Device;
    class SwapChain;
    class AsyncComputeManager;
    class ThreadCommandPoolManager;
}

namespace render
{
    class OffScreenViewPort;
    class RenderPassHandler;
}

namespace render::gpudriven
{
    class BrushComputePipeline;
    class HydraulicErosionPipeline;
}

// VK-1594: taken by const reference only, so the definition stays in the .cpp
namespace render::mesh
{
    struct InMemoryMeshData;
}

namespace services
{
    class IVFXRuntimeProvider;
    class ITerrainRenderProvider;
    class IOceanRenderProvider;
    class IGrassRenderProvider;
    class IVegetationRenderProvider;
}

namespace controllers::offscreen
{
    class IBLController;
    class MeshAssetManager;
    class CameraController;
    class SceneBVHManager;
    class LightBVHManager;
    class FramePreparationSystem;
    class CullingStatsCollector;
}

namespace controllers
{
    class OffScreenController
    {
    private:
        core::SwapChain& swapChain;
        core::Device& device;
        std::unique_ptr<render::OffScreenViewPort> offScreen;

        std::unique_ptr<offscreen::IBLController> iblController;
        std::unique_ptr<offscreen::MeshAssetManager> meshAssetManager;
        std::unique_ptr<offscreen::CameraController> cameraController;
        std::unique_ptr<offscreen::SceneBVHManager> bvhManager;
        std::unique_ptr<offscreen::LightBVHManager> lightBvhManager;
        std::unique_ptr<offscreen::FramePreparationSystem> framePreparation;
        std::unique_ptr<offscreen::CullingStatsCollector> statsCollector;

        std::unique_ptr<events::SubscriptionToken> materialSavedSubscription;
        std::unique_ptr<events::SubscriptionToken> editorModeChangedSubscription;
        std::unique_ptr<events::SubscriptionToken> sceneClearedSubscription;
        std::unique_ptr<events::SubscriptionToken> terrainDeletedSubscription;
        std::unique_ptr<events::SubscriptionToken> tileRemovedSubscription;
        std::unique_ptr<events::SubscriptionToken> waterDeletedSubscription;
        std::unique_ptr<events::SubscriptionToken> entitySelectedSubscription;

        bool showBillboardIcons = true;
        bool showDebugRendering = true;
        bool showGrid = true;
        bool showPhysicsDebug = false;
        bool showClusterDebug = false;
        bool showShadowDebug = false;
        bool showWireframe = false;
        bool playModeActive = false;
        bool prevLeftMouseDown = false;
        glm::vec2 uiViewportOffset{0.0f, 0.0f};
        glm::vec2 uiViewportPanelSize{0.0f, 0.0f};
        postprocess::PostProcessSettings currentPostProcessSettings;
        postprocess::VolumetricQuality activeVolumetricQuality = postprocess::VolumetricQuality::Medium;
        render::atmosphere::AtmosphereSettings currentAtmosphereSettings;
        render::cloud::CloudSettings currentCloudSettings;

        // Scene-load fires ApplyShadowSettingsCommand before the GPU-driven renderer / shadow
        // system finish initializing, which would silently drop shadow quality + culling (the
        // renderer comes up on defaults). Cache the latest settings and re-apply once per frame
        // until the shadow system is initialized, then stop.
        types::RenderSettings pendingRenderSettings;
        bool hasPendingRenderSettings = false;

    public:
        explicit OffScreenController();
        ~OffScreenController();

        void init();
        void recreate();
        void cleanUp();

        void iblSet(std::string_view iblPath);
        void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
        void iblSetParams(float intensity, float rotationDeg, const glm::vec3& tint); // VK-1574
        void iblRemove();

        std::string meshLoad(std::string_view meshPath);
        void meshUnload(const std::string& meshId);
        void meshRelease(const std::string& meshPath);
        void textureRelease(const std::string& texturePath);
        void materialRelease(const std::string& materialPath);
        void meshUpdateCamera(render::occlusion::CameraId cameraId, const glm::mat4& view,
                              const glm::mat4& projection, const glm::vec3& cameraPos, float time = 0.0f);
        bool isMeshLoaded(const std::string& meshPath) const;
        std::vector<std::string> getLoadedMeshes() const;
        std::optional<services::MeshBounds> getMeshBoundingBox(const std::string& meshPath) const;

        void prepareCameras();
        void prepareFrameMeshes();
        void prepareFrameBillboards();
        void prepareFrameText();
        void setFontFallbackChain(std::span<const std::string> fontPaths);
        void invalidateFont(const std::string& fontPath);
        void prepareSceneData();
        void prepareFrameCameraFrustums();
        void prepareFrameAudioSpheres();
        void prepareFrameLightGizmos();

        void setShowBillboardIcons(bool show) { showBillboardIcons = show; }
        bool getShowBillboardIcons() const { return showBillboardIcons; }
        bool loadBillboardAtlas(const std::string& atlasPath);
        void setBillboardRenderingEnabled(bool enabled);

        void setDecalRenderingEnabled(bool enabled);
        void setDecalDrawList(const std::vector<services::DecalRenderData>& decals);

        void setOcclusionCullingEnabled(bool enabled);

        void removeCamera(render::occlusion::CameraId id);

        void* render();
        void* render(const std::function<void()>& preRenderCallback);

        // Get the offscreen color image for runtime blit (returns VkImage as void*)
        void* getColorImage(uint32_t imageIndex) const;

        services::CullingDebugStats getCullingStats() const;

        void applyShadowSettings(const types::RenderSettings& settings);
        // Applies whatever it can right now; returns true only once the shadow system is
        // initialized (i.e. shadow quality/pages actually took effect). Drives the retry.
        bool applyRenderSettingsInternal(const types::RenderSettings& settings);
        // Re-applies cached settings each frame until they fully land. Called from render().
        void retryPendingRenderSettings();
        services::ShadowStats getShadowStats() const;
        types::RTShadowStats getRTShadowStats() const;
        services::GPUPipelineStatus getGPUPipelineStatus() const;

        void setPlayMode(bool playMode);
        void waitForIdle();

        void setShowDebugRendering(bool show) { showDebugRendering = show; }
        bool getShowDebugRendering() const { return showDebugRendering; }

        void setShowGrid(bool show);
        bool getShowGrid() const { return showGrid; }
        void prepareGrid();

        void setShowPhysicsDebug(bool show);
        bool getShowPhysicsDebug() const { return showPhysicsDebug; }
        void prepareFramePhysicsColliders();

        // VK-1490: editor selection (raw entt ids) for the silhouette outline
        // passes. Forced empty while play mode is active.
        void setSelectedEntities(std::vector<uint32_t> entityIds);

        void setViewMode(uint32_t mode);
        uint32_t getViewMode() const;

        void setFrustumCullingEnabled(bool enabled);
        void setLODSelectionEnabled(bool enabled);
        void setLODCrossfadeEnabled(bool enabled);
        void setMeshletFrustumCullingEnabled(bool enabled);
        void setMeshletBackfaceCullingEnabled(bool enabled);
        void setMeshletOcclusionCullingEnabled(bool enabled);
        void setDistanceCullingEnabled(bool enabled);
        void setCategoryDistance(uint32_t category, float distance);
        void setShadowDistanceMultiplier(float multiplier);
        void setGlobalLodBias(float bias);
        void setFoliageDensityScale(float scale); // VK-1582
        void setTerrainFrustumCullingEnabled(bool enabled);
        void setTerrainMeshletCullingEnabled(bool enabled);

        void setWBOITEnabled(bool enabled);

        void setTerrainRenderingEnabled(bool enabled);
        void setTerrainLODBias(float bias);
        void setTerrainErrorThreshold(float threshold);
        void setTerrainTextureScale(float scale);

        void setShowClusterDebug(bool show) { showClusterDebug = show; }
        bool getShowClusterDebug() const { return showClusterDebug; }
        void prepareFrameClusterDebug();

        void setShowShadowDebug(bool show) { showShadowDebug = show; }
        bool getShowShadowDebug() const { return showShadowDebug; }
        void prepareFrameShadowDebug();

        void setShowWireframe(bool show);
        bool getShowWireframe() const { return showWireframe; }

        void setShowNavmeshDebug(bool show);
        bool getShowNavmeshDebug() const;
        void updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices);
        void clearNavmeshDebugMesh();

        void setWorldMaskDebugEnabled(bool enabled);
        bool getWorldMaskDebugEnabled() const;

        void updateImmediateDebugDrawList(render::mesh::ImmediateDebugDrawList drawList);

        void prepareFrameUICanvasOutlines();
        void prepareFrameUIImages();

        void setUIViewportOffset(const glm::vec2& offset, const glm::vec2& panelSize)
        {
            uiViewportOffset = offset;
            uiViewportPanelSize = panelSize;
        }

        void applyPostProcessSettings(const postprocess::PostProcessSettings& settings);
        postprocess::PostProcessSettings getPostProcessSettings() const;
        void setPostProcessEnabled(bool enabled);
        bool isPostProcessEnabled() const;

        void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider);
        void setTerrainRenderProvider(services::ITerrainRenderProvider* provider);
        void setOceanRenderProvider(services::IOceanRenderProvider* provider);
        void setGrassRenderProvider(services::IGrassRenderProvider* provider);
        void setVegetationRenderProvider(services::IVegetationRenderProvider* provider);

        void setRaycastCursorUV(const glm::vec2& uv);
        void clearRaycastCursor();
        terrain::TerrainHitResult getTerrainHitResult() const;

        void setBrushOverlayParams(float radius, float falloff, float shape, float stampRotation = 0.0f);

        bool applyBrushGPU(
            std::vector<float>& heightData,
            const terrain::BrushGPUParams& params);

        bool applyHydraulicErosionGPU(
            std::vector<float>& field,
            const std::vector<uint32_t>& validMask,
            const terrain::HydraulicGPUParams& params);

        void setStampData(
            const std::vector<float>& heights,
            uint32_t width, uint32_t height);
        void clearStampData();

        void setStampOverlay(vk::Buffer buffer, uint32_t width, uint32_t height, float rotation);
        void clearStampOverlay();

        render::RenderPassHandler* getRenderPassHandler() const;

        plugin::RenderHookHandle registerRenderHook(plugin::RenderPassHookPoint hookPoint,
                                                     plugin::RenderHookCallback callback);
        void unregisterRenderHook(plugin::RenderHookHandle handle);

        void addTerrainFrustum(const glm::mat4& viewProjection, const glm::vec3& cameraPos);
        void clearAdditionalTerrainFrustums();

            // GI settings
        void applyGISettings(const render::gi::GISettings& settings);
        render::gi::GISettings getGISettings() const;
        render::gi::GIDebugStats getGIDebugStats() const;
        void setGIShowProbes(bool show);
        void setGIShowCascadeBounds(bool show);
        void setGIShowProbeValidity(bool show);

        // Atmosphere settings
        void applyAtmosphereSettings(const render::atmosphere::AtmosphereSettings& settings);
        render::atmosphere::AtmosphereSettings getAtmosphereSettings() const;

        // Cloud settings
        void applyCloudSettings(const render::cloud::CloudSettings& settings);
        render::cloud::CloudSettings getCloudSettings() const;

        // Weather surface effects
        void setSnowAccumulation(float value);
        void setWetness(float value);

        // Light streaming settings
        void setLightStreamingConfig(const render::lighting::LightStreamingConfig& config);
        render::lighting::LightStreamingConfig getLightStreamingConfig() const;
        render::lighting::LightStreamingStats getLightStreamingStats() const;
        void registerSectorLights(uint32_t sectorId, const std::vector<uint32_t>& lightEntityIds);
        void unregisterSectorLights(uint32_t sectorId);

        // Object streaming settings
        void setObjectStreamingEnabled(bool enabled);
        void setObjectStreamingConfig(const render::gpudriven::ObjectStreamConfig& config);
        render::gpudriven::ObjectStreamConfig getObjectStreamingConfig() const;
        render::gpudriven::ObjectStreamingStats getObjectStreamingStats() const;
        void registerSectorObjects(uint32_t sectorId,
                                   const std::vector<std::pair<uint64_t, entt::entity>>& entities);
        void unregisterSectorObjects(uint32_t sectorId);

        // VK-1594: baked HLOD proxy geometry pushed into MergedMeshBuffer from memory
        bool registerHLODMesh(const render::mesh::InMemoryMeshData& meshData);
        void releaseHLODMesh(const std::string& meshKey);

    private:
        std::unique_ptr<render::gpudriven::BrushComputePipeline> brushComputePipeline;
        std::unique_ptr<render::gpudriven::HydraulicErosionPipeline> hydraulicErosionPipeline;
        std::unique_ptr<core::AsyncComputeManager> asyncComputeManager;
        // Scene recording pools (separate from ShadowSystem's pools for VSM tiles)
        std::unique_ptr<core::ThreadCommandPoolManager> sceneThreadPoolManager;
    };
}
