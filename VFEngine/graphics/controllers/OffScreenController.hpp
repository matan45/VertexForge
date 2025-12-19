#pragma once
#include <glm/glm.hpp>
#include "math/Frustum.hpp"
#include "scene/SceneBVH.hpp"
#include <memory>
#include <string_view>
#include <string>
#include <vector>
#include <cstdint>

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
        void meshUpdateCamera(const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos, float time = 0.0f);
        bool isMeshLoaded(const std::string& meshPath) const;
        std::vector<std::string> getLoadedMeshes() const;

        // Called each frame to prepare mesh render list from ECS entities
        void prepareFrameMeshes();

        // BVH management
        void rebuildBVH();        // Force rebuild BVH
        void markBVHDirty();      // Mark BVH for rebuild (call when entities change)

        // Occlusion culling control
        void setOcclusionCullingEnabled(bool enabled) { occlusionCullingEnabled = enabled; }
        bool isOcclusionCullingEnabled() const { return occlusionCullingEnabled; }

        void* render();

    private:
        void updateOcclusionCullingData();
    };
}
