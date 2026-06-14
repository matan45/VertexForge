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

    using PreRenderCallback = std::function<void(uint32_t imageIndex)>;

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

        // Copy the just-rendered offscreen color image into a freshly allocated
        // square image of `size` px and return a persistent ImGui descriptor for
        // it (VK-1379 thumbnails). Unlike render(), the returned texture is owned
        // and is NOT overwritten by subsequent renders. Release with releaseSnapshot().
        void* snapshot(uint32_t size);
        void releaseSnapshot(void* descriptor);

        PreviewRenderHandler* getRenderHandler() const { return renderHandler.get(); }
        uint32_t getImageCount() const { return static_cast<uint32_t>(offscreenResources.colorImages.size()); }

    private:
        struct SnapshotImage
        {
            vk::Image image;
            core::VulkanAllocation allocation;
            vk::ImageView view;
            vk::DescriptorSet descriptor;
        };
        std::vector<SnapshotImage> snapshots;

        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void cleanupSnapshots();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;
        void createSampler();
    };
}
