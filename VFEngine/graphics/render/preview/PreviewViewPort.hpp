#pragma once
#include "../../core/OffScreen.hpp"
#include <memory>
#include <functional>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
}

namespace render::preview
{
    class PreviewRenderHandler;

    using PreRenderCallback = std::function<void()>;

    class PreviewViewPort
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<core::CommandPool> commandPool;

        std::unique_ptr<PreviewRenderHandler> renderHandler;

        vk::Sampler sampler;
        core::OffscreenResources offscreenResources;
        std::vector<vk::Fence> inFlightFences;

    public:
        explicit PreviewViewPort(core::Device& device, core::SwapChain& swapChain);
        ~PreviewViewPort();

        void init();
        void recreate();
        vk::DescriptorSet render(const PreRenderCallback& preRenderCallback = nullptr);
        void cleanUp();

        PreviewRenderHandler* getRenderHandler() const { return renderHandler.get(); }

    private:
        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;
        void createSampler();
    };
}
