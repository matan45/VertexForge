#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string_view>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <functional>
#include <span>
#include <utility>
#include <entt/entt.hpp>
#include "providers/render/IOffScreenProvider.hpp"
#include "types/CameraTypes.hpp"
#include "terrain/TerrainHitResult.hpp"
#include "terrain/BrushTypes.hpp"
#include "terrain/TerrainHydraulicErosion.hpp"
#include "postprocess/PostProcessTypes.hpp"
#include "../../graphics/render/gi/GITypes.hpp"
#include "atmosphere/AtmosphereSettings.hpp"
#include "../../graphics/render/lighting/LightStreamManager.hpp"
#include "../../graphics/render/gpudriven/scene/GPUObjectStreamTypes.hpp"
#include "data/RenderHookTypes.hpp"
#include "../../graphics/render/tools/ImmediateDebugTypes.hpp"
#include "providers/render/IDecalRenderProvider.hpp"

namespace render
{
    class RenderPassHandler;
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

namespace controllers
{
    using types::CameraId;
    using types::MAIN_CAMERA_ID;

    class OffScreenController;

    class OffScreen
    {
    private:
        std::unique_ptr<controllers::OffScreenController> offScreenController;

    public:
        explicit OffScreen();
        ~OffScreen();

        void init();
        void recreate();
        void cleanUp();

        void* render();
        void* render(const std::function<void()>& preRenderCallback);

        // Get the offscreen color image for runtime blit (returns VkImage as void*)
        void* getColorImage(uint32_t imageIndex) const;

        void iblSet(std::string_view iblPath);
        void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
        void iblSetParams(float intensity, float rotationDeg, const glm::vec3& tint); // VK-1574
        void iblRemove();

        std::string meshLoad(std::string_view meshPath);
        void meshUnload(const std::string& meshId);
        void meshRelease(const std::string& meshPath);
        void textureRelease(const std::string& texturePath);
        void materialRelease(const std::string& materialPath);
        void meshUpdateCamera(CameraId cameraId, const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos, float time = 0.0f);
        bool isMeshLoaded(const std::string& meshPath) const;
        std::vector<std::string> getLoadedMeshes() const;
        void prepareCameras();
        std::optional<services::MeshBounds> getMeshBoundingBox(const std::string& meshPath) const;
        void prepareFrameMeshes();

        void removeCamera(CameraId id);
        void prepareFrameCameraFrustums();
        void prepareFrameAudioSpheres();
        void prepareFrameLightGizmos();

        void prepareFrameBillboards();
        void prepareFrameText();
        void setFontFallbackChain(std::span<const std::string> fontPaths);
        void invalidateFont(const std::string& fontPath);
        void prepareSceneData();
        void setShowBillboardIcons(bool show);
        bool getShowBillboardIcons() const;
        bool loadBillboardAtlas(const std::string& atlasPath);

        services::CullingDebugStats getCullingStats() const;

        void applyShadowSettings(const types::RenderSettings& settings);
        services::ShadowStats getShadowStats() const;
        types::RTShadowStats getRTShadowStats() const;
        services::GPUPipelineStatus getGPUPipelineStatus() const;

        void setPlayMode(bool playMode);
        void waitForIdle();

        void setShowDebugRendering(bool show);
        bool getShowDebugRendering() const;

        void setShowGrid(bool show);
        bool getShowGrid() const;
        void prepareGrid();

        void setShowPhysicsDebug(bool show);
        bool getShowPhysicsDebug() const;
        void prepareFramePhysicsColliders();

        // VK-1490: editor selection (raw entt ids) for the silhouette outline
        // passes. Empty clears the highlight.
        void setSelectedEntities(std::vector<uint32_t> entityIds);

        void setViewMode(uint32_t mode);
        uint32_t getViewMode() const;

        void setShowClusterDebug(bool show);
        bool getShowClusterDebug() const;
        void prepareFrameClusterDebug();

        void setShowShadowDebug(bool show);
        bool getShowShadowDebug() const;
        void prepareFrameShadowDebug();

        void setShowWireframe(bool show);
        bool getShowWireframe() const;

        void setShowNavmeshDebug(bool show);
        bool getShowNavmeshDebug() const;
        void updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices);
        void clearNavmeshDebugMesh();

        void setWorldMaskDebugEnabled(bool enabled);
        bool getWorldMaskDebugEnabled() const;

        void updateImmediateDebugDrawList(render::mesh::ImmediateDebugDrawList drawList);

        void prepareFrameUICanvasOutlines();
        void prepareFrameUIImages();

        void setFrustumCullingEnabled(bool enabled);
        void setOcclusionCullingEnabled(bool enabled);
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

        void setBillboardRenderingEnabled(bool enabled);

        void setDecalRenderingEnabled(bool enabled);
        void setDecalDrawList(const std::vector<services::DecalRenderData>& decals);

        void setUIViewportOffset(const glm::vec2& offset, const glm::vec2& panelSize);

        void applyPostProcessSettings(const postprocess::PostProcessSettings& settings);
        postprocess::PostProcessSettings getPostProcessSettings() const;
        void setPostProcessEnabled(bool enabled);
        bool isPostProcessEnabled() const;

        // Atmosphere settings
        void applyAtmosphereSettings(const render::atmosphere::AtmosphereSettings& settings);
        render::atmosphere::AtmosphereSettings getAtmosphereSettings() const;

        // Cloud settings
        void applyCloudSettings(const render::cloud::CloudSettings& settings);
        render::cloud::CloudSettings getCloudSettings() const;

        // Weather surface effects
        void setSnowAccumulation(float value);
        void setWetness(float value);

        // GI settings
        void applyGISettings(const render::gi::GISettings& settings);
        render::gi::GISettings getGISettings() const;
        render::gi::GIDebugStats getGIDebugStats() const;
        void setGIShowProbes(bool show);
        void setGIShowCascadeBounds(bool show);
        void setGIShowProbeValidity(bool show);

        // Light streaming settings
        void setLightStreamingConfig(const render::lighting::LightStreamingConfig& config);
        render::lighting::LightStreamingConfig getLightStreamingConfig() const;
        render::lighting::LightStreamingStats getLightStreamingStats() const;
        void registerSectorLights(uint64_t sectorId, const std::vector<uint32_t>& lightEntityIds);
        void unregisterSectorLights(uint64_t sectorId);

        // Object streaming settings
        void setObjectStreamingEnabled(bool enabled);
        void setObjectStreamingConfig(const render::gpudriven::ObjectStreamConfig& config);
        render::gpudriven::ObjectStreamConfig getObjectStreamingConfig() const;
        render::gpudriven::ObjectStreamingStats getObjectStreamingStats() const;
        void registerSectorObjects(uint64_t sectorId,
                                   const std::vector<std::pair<uint64_t, entt::entity>>& entities);
        void unregisterSectorObjects(uint64_t sectorId);

        // VK-1594: baked HLOD proxy geometry, uploaded from memory under a synthetic mesh key
        bool registerHLODMesh(const render::mesh::InMemoryMeshData& mesh);
        void releaseHLODMesh(const std::string& meshKey);

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

        render::RenderPassHandler* getRenderPassHandler() const;

        plugin::RenderHookHandle registerRenderHook(plugin::RenderPassHookPoint hookPoint,
                                                     plugin::RenderHookCallback callback);
        void unregisterRenderHook(plugin::RenderHookHandle handle);

        void addTerrainFrustum(const glm::mat4& viewProjection, const glm::vec3& cameraPos);
        void clearAdditionalTerrainFrustums();
    };
}
