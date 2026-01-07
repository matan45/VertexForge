#pragma once
#include <glm/glm.hpp>
#include "../../services/providers/IOffScreenProvider.hpp"
#include "../render/occlusion/CameraOcclusionManager.hpp"
#include <memory>
#include <string_view>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace events { struct SubscriptionToken; }

namespace core
{
    class Device;
    class SwapChain;
}

namespace render
{
    class OffScreenViewPort;
}

namespace controllers::offscreen
{
    class IBLController;
    class MeshAssetManager;
    class CameraController;
    class SceneBVHManager;
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

        // Extracted managers
        std::unique_ptr<offscreen::IBLController> iblController;
        std::unique_ptr<offscreen::MeshAssetManager> meshAssetManager;
        std::unique_ptr<offscreen::CameraController> cameraController;
        std::unique_ptr<offscreen::SceneBVHManager> bvhManager;
        std::unique_ptr<offscreen::FramePreparationSystem> framePreparation;
        std::unique_ptr<offscreen::CullingStatsCollector> statsCollector;

        // Event subscription for material cache invalidation
        std::unique_ptr<events::SubscriptionToken> materialSavedSubscription;

        // UI state flags
        bool showBillboardIcons = true;
        bool showDebugRendering = true;
        bool showGrid = true;
        bool playModeActive = false;

    public:
        explicit OffScreenController();
        ~OffScreenController();

        void init();
        void recreate();
        void cleanUp() const;

        // IBL API
        void iblSet(std::string_view iblPath);
        void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
        void iblRemove();

        // Mesh API
        std::string meshLoad(std::string_view meshPath);
        void meshUnload(const std::string& meshId);
        void meshUpdateCamera(render::occlusion::CameraId cameraId, const glm::mat4& view,
                              const glm::mat4& projection, const glm::vec3& cameraPos, float time = 0.0f);
        bool isMeshLoaded(const std::string& meshPath) const;
        std::vector<std::string> getLoadedMeshes() const;
        std::optional<services::MeshBounds> getMeshBoundingBox(const std::string& meshPath) const;

        // Called each frame to sync CameraComponents with occlusion system
        void prepareCameras();

        void prepareFrameMeshes();
        void prepareFrameBillboards();
        void prepareFrameCameraFrustums();
        void prepareFrameAudioSpheres();

        // Billboard visibility toggle
        void setShowBillboardIcons(bool show) { showBillboardIcons = show; }
        bool getShowBillboardIcons() const { return showBillboardIcons; }

        bool loadBillboardAtlas(const std::string& atlasPath);

        // BVH management
        void rebuildBVH();
        void markBVHDirty();

        // Occlusion culling control (main camera)
        void setOcclusionCullingEnabled(bool enabled);
        bool isOcclusionCullingEnabled() const;

        // Multi-camera support for occlusion culling
        void createCamera(render::occlusion::CameraId id, bool enableOcclusion = false);
        void removeCamera(render::occlusion::CameraId id);
        void setActiveCamera(render::occlusion::CameraId id);
        render::occlusion::CameraId getActiveCameraId() const;

        void* render();

        // Debug/Stats API
        services::CullingDebugStats getCullingStats() const;

        // Editor Mode API
        void setPlayMode(bool playMode);
        bool isPlayMode() const { return playModeActive; }

        // Debug Rendering API
        void setShowDebugRendering(bool show) { showDebugRendering = show; }
        bool getShowDebugRendering() const { return showDebugRendering; }

        // Grid API
        void setShowGrid(bool show);
        bool getShowGrid() const { return showGrid; }
        void prepareGrid();

        // View Mode API
        void setViewMode(uint32_t mode);
        uint32_t getViewMode() const;
    };
}
