#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string_view>

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

    public:
        explicit OffScreenController();
        ~OffScreenController();

        void init();
        void cleanUp() const;

        // Initialize IBL with HDR path only
        void iblSet(std::string_view iblPath);
        
        // Update camera matrices for IBL rendering
        void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
        
        void iblRemove();

        void* render();
    };
}
