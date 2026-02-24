#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    struct OffscreenResources;
}

namespace render::transparency
{
    class WBOITPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;

        // WBOIT render targets
        vk::Image accumImage;
        vk::DeviceMemory accumMemory;
        vk::ImageView accumImageView;

        vk::Image revealageImage;
        vk::DeviceMemory revealageMemory;
        vk::ImageView revealageImageView;

        // WBOIT render pass (2 color attachments + depth read-only)
        vk::RenderPass wboitRenderPass;
        std::vector<vk::Framebuffer> wboitFramebuffers;

        // Composite pass
        vk::RenderPass compositeRenderPass;
        std::vector<vk::Framebuffer> compositeFramebuffers;
        vk::Pipeline compositePipeline;
        vk::PipelineLayout compositePipelineLayout;

        vk::DescriptorSetLayout compositeDescriptorSetLayout;
        vk::DescriptorPool compositeDescriptorPool;
        vk::DescriptorSet compositeDescriptorSet;

        vk::Sampler linearSampler;

    public:
        WBOITPipeline(core::Device& device, core::SwapChain& swapChain,
                      core::OffscreenResources& offscreenResources);
        ~WBOITPipeline();

        void init();
        void cleanup();
        void recreate();

        void beginWBOITPass(const vk::CommandBuffer& cmd, uint32_t imageIndex);
        void endWBOITPass(const vk::CommandBuffer& cmd);
        void composite(const vk::CommandBuffer& cmd, uint32_t imageIndex);

        vk::RenderPass getWBOITRenderPass() const { return wboitRenderPass; }
        bool isInitialized() const { return initialized; }

    private:
        void createRenderTargets();
        void createWBOITRenderPass();
        void createWBOITFramebuffers();
        void createCompositeRenderPass();
        void createCompositeFramebuffers();
        void createSampler();
        void createCompositeDescriptorResources();
        void createCompositePipeline();
        void cleanupRenderTargets();
    };
}
