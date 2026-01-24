#pragma once
#include "../core/OffScreen.hpp"
#include <vector>
#include <memory>
#include <functional>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
}

namespace render
{
    class RenderPassHandler;

    // Callback invoked after fence wait but before command recording
    // Useful for safe descriptor set updates
    using PreRenderCallback = std::function<void()>;

    class OffScreenViewPort
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<core::CommandPool> commandPool;

        std::unique_ptr<render::RenderPassHandler> renderPassHandler;

        vk::Sampler sampler;
        core::OffscreenResources offscreenResources;

        // Per-frame fences to ensure command buffers aren't reused while in flight
        std::vector<vk::Fence> inFlightFences;

    public:
        explicit OffScreenViewPort(core::Device& device, core::SwapChain& swapChain);
        ~OffScreenViewPort();

        void init();
        void recreate();

        // Render with optional pre-render callback (called after fence wait, before command recording)
        // Use for safe descriptor set updates
        vk::DescriptorSet render(const PreRenderCallback& preRenderCallback = nullptr);

        void cleanUp();
        render::RenderPassHandler* getRenderPassHandler() const { return renderPassHandler.get(); }

    private:
        void draw(const vk::CommandBuffer& commandBuffer) const;

        void createOffscreenResources();
        void cleanupOffscreenResources();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;
        void createSampler();
    };
}
