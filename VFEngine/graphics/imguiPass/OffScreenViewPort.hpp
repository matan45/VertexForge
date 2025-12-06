#pragma once
#include "../core/OffScreen.hpp"
#include <vector>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
}

namespace render
{
    class RenderPassHandler;
}

namespace imguiPass
{
    class OffScreenViewPort
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<core::CommandPool> commandPool;

        std::unique_ptr<render::RenderPassHandler> renderPassHandler;

        vk::Sampler sampler;
        core::OffscreenResources offscreenResources;

    public:
        explicit OffScreenViewPort(core::Device& device, core::SwapChain& swapChain);
        ~OffScreenViewPort();

        void init();
        vk::DescriptorSet render();
        void cleanUp() const;
        const render::RenderPassHandler* getRenderPassHandler() const { return renderPassHandler.get(); }

    private:
        void draw(const vk::CommandBuffer& commandBuffer) const;

        void createOffscreenResources();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;
        void createSampler();
    };
}
