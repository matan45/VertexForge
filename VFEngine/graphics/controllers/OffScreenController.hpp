#pragma once
#include <memory>
#include "components/Components.hpp"


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

        void iblAdd(std::string_view iblPath, components::CameraComponent* camera);
        void iblRemove();

        void* render();
    };
}
