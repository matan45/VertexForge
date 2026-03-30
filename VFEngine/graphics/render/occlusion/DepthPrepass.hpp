#pragma once
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <cstdint>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::occlusion
{
    class DepthPrepass
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        vk::Image depthImage;
        core::VulkanAllocation depthAllocation;
        vk::ImageView depthImageView;

        vk::Image normalImage;
        core::VulkanAllocation normalAllocation;
        vk::ImageView normalImageView;

        vk::RenderPass renderPass;
        vk::Framebuffer framebuffer;

        uint32_t width = 0;
        uint32_t height = 0;
        bool initialized = false;

    public:
        explicit DepthPrepass(core::Device& device, core::SwapChain& swapChain);
        ~DepthPrepass();

        DepthPrepass(const DepthPrepass&) = delete;
        DepthPrepass& operator=(const DepthPrepass&) = delete;

        void init(uint32_t width, uint32_t height);
        void cleanup();

        void beginPass(vk::CommandBuffer cmd) const;
        void endPass(vk::CommandBuffer cmd) const;

        vk::Image getDepthImage() const { return depthImage; }
        vk::ImageView getDepthImageView() const { return depthImageView; }
        vk::Image getNormalImage() const { return normalImage; }
        vk::ImageView getNormalImageView() const { return normalImageView; }
        vk::RenderPass getRenderPass() const { return renderPass; }
        uint32_t getWidth() const { return width; }
        uint32_t getHeight() const { return height; }
        bool isInitialized() const { return initialized; }

    private:
        void createImages();
        void transitionInitialLayouts();
        void createRenderPass();
        void createFramebuffer();
    };
}
