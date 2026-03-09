#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string_view>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "providers/render/IOffScreenProvider.hpp"
#include "types/CameraTypes.hpp"
#include "terrain/TerrainHitResult.hpp"
#include "terrain/BrushTypes.hpp"
#include "postprocess/PostProcessTypes.hpp"
#include "../../graphics/render/gi/GITypes.hpp"
#include "../../graphics/render/lighting/LightStreamManager.hpp"
#include "data/RenderHookTypes.hpp"
#include "../../graphics/render/tools/ImmediateDebugTypes.hpp"

namespace render
{
    class RenderPassHandler;
}

namespace services
{
    class IVFXRuntimeProvider;
    class ITerrainRenderProvider;
    class IWaterRenderProvider;
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

        // Get the offscreen color image for runtime blit (returns VkImage as void*)
        void* getColorImage(uint32_t imageIndex) const;

        void iblSet(std::string_view iblPath);
        void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
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
        void setShowBillboardIcons(bool show);
        bool getShowBillboardIcons() const;
        bool loadBillboardAtlas(const std::string& atlasPath);

        services::CullingDebugStats getCullingStats() const;

        void applyShadowSettings(const types::RenderSettings& settings);
        services::ShadowStats getShadowStats() const;

        void setPlayMode(bool playMode);

        void setShowDebugRendering(bool show);
        bool getShowDebugRendering() const;

        void setShowGrid(bool show);
        bool getShowGrid() const;
        void prepareGrid();

        void setShowPhysicsDebug(bool show);
        bool getShowPhysicsDebug() const;
        void prepareFramePhysicsColliders();

        void setViewMode(uint32_t mode);
        uint32_t getViewMode() const;

        void setShowClusterDebug(bool show);
        bool getShowClusterDebug() const;
        void prepareFrameClusterDebug();

        void setShowShadowDebug(bool show);
        bool getShowShadowDebug() const;
        void prepareFrameShadowDebug();

        void setShowNavmeshDebug(bool show);
        bool getShowNavmeshDebug() const;
        void updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices);
        void clearNavmeshDebugMesh();

        void updateImmediateDebugDrawList(render::mesh::ImmediateDebugDrawList drawList);

        void prepareFrameUICanvasOutlines();
        void prepareFrameUIImages();

        void setFrustumCullingEnabled(bool enabled);
        void setOcclusionCullingEnabled(bool enabled);
        void setLODSelectionEnabled(bool enabled);
        void setMeshletFrustumCullingEnabled(bool enabled);
        void setMeshletBackfaceCullingEnabled(bool enabled);
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
        void setTerrainShadowLOD(uint32_t lod);

        void setBillboardRenderingEnabled(bool enabled);

        void setUIViewportOffset(const glm::vec2& offset, const glm::vec2& panelSize);

        void applyPostProcessSettings(const postprocess::PostProcessSettings& settings);
        postprocess::PostProcessSettings getPostProcessSettings() const;
        void setPostProcessEnabled(bool enabled);
        bool isPostProcessEnabled() const;

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
        void registerSectorLights(uint32_t sectorId);
        void unregisterSectorLights(uint32_t sectorId);

        void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider);
        void setTerrainRenderProvider(services::ITerrainRenderProvider* provider);
        void setWaterRenderProvider(services::IWaterRenderProvider* provider);
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

        void addWaterFrustum(const glm::mat4& viewProjection, const glm::vec3& cameraPos);
        void clearAdditionalWaterFrustums();

        struct ImposterBakeResult
        {
            bool success = false;
            std::string outputPath;
            std::string errorMessage;
        };
        ImposterBakeResult bakeImposter(const std::string& meshPath, const std::string& outputPath,
                                        const glm::vec3& meshCenter = glm::vec3(0.0f), float meshScale = 1.0f);
    };
}
