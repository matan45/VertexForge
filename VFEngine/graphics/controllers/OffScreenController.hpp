#pragma once
#include <glm/glm.hpp>
#include "math/Frustum.hpp"
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
        std::unique_ptr<events::SubscriptionToken> materialSavedSubscription;  // Subscription token for material saved notification
        bool showBillboardIcons = true;  // Toggle for billboard icon visibility

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

        // Called each frame to prepare billboard render list from ECS entities
        void prepareFrameBillboards();

        // Billboard visibility toggle
        void setShowBillboardIcons(bool show) { showBillboardIcons = show; }
        bool getShowBillboardIcons() const { return showBillboardIcons; }

        // Billboard atlas loading
        bool loadBillboardAtlas(const std::string& atlasPath);

        void* render();
    };
}
