#pragma once
#include <glm/glm.hpp>
#include "../../services/providers/render/IOffScreenProvider.hpp"
#include "../../services/data/RenderHookTypes.hpp"
#include "../render/occlusion/CameraOcclusionManager.hpp"
#include "terrain/TerrainHitResult.hpp"
#include "terrain/BrushTypes.hpp"
#include "postprocess/PostProcessTypes.hpp"
#include "../render/gi/GITypes.hpp"
#include "atmosphere/AtmosphereSettings.hpp"
#include "cloud/CloudSettings.hpp"
#include "../render/lighting/LightStreamManager.hpp"
#include "../render/gpudriven/scene/GPUObjectStreamTypes.hpp"
#include "../render/tools/ImmediateDebugTypes.hpp"
#include "../../services/providers/render/IDecalRenderProvider.hpp"
#include <memory>
#include <string_view>
#include <string>
#include <vector>
#include <utility>
#include <optional>
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

    public:
        explicit OffScreenController();
        ~OffScreenController();

        void init();
        void recreate();
        void cleanUp();

        void iblSet(std::string_view iblPath);
        void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
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

        // Get the offscreen color image for runtime blit (returns VkImage as void*)
        void* getColorImage(uint32_t imageIndex) const;

        services::CullingDebugStats getCullingStats() const;

        void applyShadowSettings(const types::RenderSettings& settings);
        services::ShadowStats getShadowStats() const;
        types::RTShadowStats getRTShadowStats() const;
        services::GPUPipelineStatus getGPUPipelineStatus() const;

        void setPlayMode(bool playMode);

        void setShowDebugRendering(bool show) { showDebugRendering = show; }
        bool getShowDebugRendering() const { return showDebugRendering; }

        void setShowGrid(bool show);
        bool getShowGrid() const { return showGrid; }
        void prepareGrid();

        void setShowPhysicsDebug(bool show);
        bool getShowPhysicsDebug() const { return showPhysicsDebug; }
        void prepareFramePhysicsColliders();

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
        void setTerrainFrustumCullingEnabled(bool enabled);
        void setTerrainMeshletCullingEnabled(bool enabled);

        void setWBOITEnabled(bool enabled);

        void setTerrainRenderingEnabled(bool enabled);
        void setTerrainLODBias(float bias);
        void setTerrainErrorThreshold(float threshold);
        void setTerrainTextureScale(float scale);
        void setTerrainSVTEnabled(bool enabled);

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

        void setBrushOverlayParams(float radius, float falloff, float shape);

        bool applyBrushGPU(
            std::vector<float>& heightData,
            const terrain::BrushGPUParams& params);

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

        // Snow accumulation
        void setSnowAccumulation(float value);

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

    private:
        std::unique_ptr<render::gpudriven::BrushComputePipeline> brushComputePipeline;
        std::unique_ptr<core::AsyncComputeManager> asyncComputeManager;
        // Scene recording pools (separate from ShadowSystem's pools for VSM tiles)
        std::unique_ptr<core::ThreadCommandPoolManager> sceneThreadPoolManager;
    };
}
