#pragma once
#include "../core/OffScreen.hpp"
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
}


namespace render
{
    class ClearColor;
    class IBL;

    class RenderPassHandler
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<ClearColor> clearColor;
        std::unique_ptr<IBL> iblRenderer;

        core::OffscreenResources& offscreenResources;

    public:
        explicit RenderPassHandler(core::Device& device, core::SwapChain& swapChain,
                                   core::OffscreenResources& offscreenResources);
        ~RenderPassHandler();

        void init();

        void recreate() const;

        IBL* getIBL() const { return iblRenderer.get(); }

        void cleanUp() const;

        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
    };
}
