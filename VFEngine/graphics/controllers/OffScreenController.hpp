#pragma once
#include <glm/glm.hpp>
#include "../../services/providers/IOffScreenProvider.hpp"
#include "../render/occlusion/CameraOcclusionManager.hpp"
#include "terrain/TerrainHitResult.hpp"
#include "terrain/BrushTypes.hpp"
#include <memory>
#include <string_view>
#include <string>
#include <vector>
#include <optional>

namespace events
{
    struct SubscriptionToken;
}

namespace core
{
    class Device;
    class SwapChain;
}

namespace render
{
    class OffScreenViewPort;
}

namespace render::gpudriven
{
    class BrushComputePipeline;
}

namespace services
{
    class IVFXRuntimeProvider;
    class ITerrainRenderProvider;
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

        bool showBillboardIcons = true;
        bool showDebugRendering = true;
        bool showGrid = true;
        bool showPhysicsDebug = false;
        bool showClusterDebug = false;
        bool showShadowDebug = false;
        bool playModeActive = false;

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
        void meshUpdateCamera(render::occlusion::CameraId cameraId, const glm::mat4& view,
                              const glm::mat4& projection, const glm::vec3& cameraPos, float time = 0.0f);
        bool isMeshLoaded(const std::string& meshPath) const;
        std::vector<std::string> getLoadedMeshes() const;
        std::optional<services::MeshBounds> getMeshBoundingBox(const std::string& meshPath) const;

        void prepareCameras();
        void prepareFrameMeshes();
        void prepareFrameBillboards();
        void prepareFrameCameraFrustums();
        void prepareFrameAudioSpheres();
        void prepareFrameLightGizmos();

        void setShowBillboardIcons(bool show) { showBillboardIcons = show; }
        bool getShowBillboardIcons() const { return showBillboardIcons; }
        bool loadBillboardAtlas(const std::string& atlasPath);

        void setOcclusionCullingEnabled(bool enabled);

        void removeCamera(render::occlusion::CameraId id);

        void* render();

        services::CullingDebugStats getCullingStats() const;

        void applyShadowSettings(const types::RenderSettings& settings);
        services::ShadowStats getShadowStats() const;

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
        void setMeshletFrustumCullingEnabled(bool enabled);
        void setMeshletBackfaceCullingEnabled(bool enabled);
        void setTerrainFrustumCullingEnabled(bool enabled);
        void setTerrainMeshletCullingEnabled(bool enabled);

        void setTerrainRenderingEnabled(bool enabled);
        void setTerrainLODBias(float bias);
        void setTerrainErrorThreshold(float threshold);
        void setTerrainTextureScale(float scale);
        void setTerrainShadowLOD(uint32_t lod);

        void setShowClusterDebug(bool show) { showClusterDebug = show; }
        bool getShowClusterDebug() const { return showClusterDebug; }
        void prepareFrameClusterDebug();

        void setShowShadowDebug(bool show) { showShadowDebug = show; }
        bool getShowShadowDebug() const { return showShadowDebug; }
        void prepareFrameShadowDebug();

        void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider);
        void setTerrainRenderProvider(services::ITerrainRenderProvider* provider);

        void setRaycastCursorUV(const glm::vec2& uv);
        void clearRaycastCursor();
        terrain::TerrainHitResult getTerrainHitResult() const;

        void setBrushOverlayParams(float radius, float falloff, float shape);

        bool applyBrushGPU(
            std::vector<float>& heightData,
            const terrain::BrushGPUParams& params);

    private:
        std::unique_ptr<render::gpudriven::BrushComputePipeline> brushComputePipeline;
    };
}
