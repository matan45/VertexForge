#pragma once
#include <glm/glm.hpp>
#include "math/Frustum.hpp"
#include "scene/SceneBVH.hpp"
#include "../render/occlusion/CameraRenderData.hpp"
#include "../../services/providers/IOffScreenProvider.hpp"
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

namespace imguiPass
{
    class OffScreenViewPort;
}

namespace controllers
{
    class OffScreenController
    {
    private:
        core::SwapChain& swapChain;
        core::Device& device;
        std::unique_ptr<imguiPass::OffScreenViewPort> offScreen;
        math::Frustum currentFrustum;  // Current camera frustum for culling
        scene::SceneBVH sceneBVH;      // BVH for spatial culling
        std::unique_ptr<events::SubscriptionToken> materialSavedSubscription;  // Subscription token for material saved notification
        bool showBillboardIcons = true; 

        // Occlusion culling state
        glm::mat4 currentViewProj{1.0f};
        float currentNearPlane = 0.1f;
        bool occlusionCullingEnabled = true;
        bool occlusionCullingReady = false;

    public:
        explicit OffScreenController();
        ~OffScreenController();

        void init();
        void cleanUp() const;

        // IBL API
        void iblSet(std::string_view iblPath);
        void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
        void iblRemove();

        // Mesh API
        std::string meshLoad(std::string_view meshPath);
        void meshUnload(const std::string& meshId);
        // Update main camera (backward compatible)
        void meshUpdateCamera(const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos, float time = 0.0f);
        // Update specific camera
        void meshUpdateCamera(render::occlusion::CameraId cameraId, const glm::mat4& view,
                              const glm::mat4& projection, const glm::vec3& cameraPos, float time = 0.0f);
        bool isMeshLoaded(const std::string& meshPath) const;
        std::vector<std::string> getLoadedMeshes() const;
        std::optional<services::MeshBounds> getMeshBoundingBox(const std::string& meshPath) const;

       
        // Called each frame to sync CameraComponents with occlusion system
        void prepareCameras();

        // Called each frame to prepare mesh render list from ECS entities
        void prepareFrameMeshes();
        
        void prepareFrameBillboards();
        
        void prepareFrameCameraFrustums();

        // Billboard visibility toggle
        void setShowBillboardIcons(bool show) { showBillboardIcons = show; }
        bool getShowBillboardIcons() const { return showBillboardIcons; }
        
        bool loadBillboardAtlas(const std::string& atlasPath);

        // BVH management
        void rebuildBVH();        // Force rebuild BVH
        void markBVHDirty();      // Mark BVH for rebuild (call when entities change)

        // Occlusion culling control (main camera)
        void setOcclusionCullingEnabled(bool enabled) { occlusionCullingEnabled = enabled; }
        bool isOcclusionCullingEnabled() const { return occlusionCullingEnabled; }

        // Multi-camera support for occlusion culling
        // Create a secondary camera (e.g., minimap) with optional occlusion culling
        void createCamera(render::occlusion::CameraId id, bool enableOcclusion = false);
        void removeCamera(render::occlusion::CameraId id);
        void setActiveCamera(render::occlusion::CameraId id);
        render::occlusion::CameraId getActiveCameraId() const;

        void* render();

    private:
        void updateOcclusionCullingData();
    };
}
