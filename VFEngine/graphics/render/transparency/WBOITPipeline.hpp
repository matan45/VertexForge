#pragma once

#include <vulkan/vulkan.hpp>
#include "../../core/VulkanMemoryManager.hpp"
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

        vk::Image accumImage;
        core::VulkanAllocation accumAllocation;
        vk::ImageView accumImageView;

        vk::Image revealageImage;
        core::VulkanAllocation revealageAllocation;
        vk::ImageView revealageImageView;

        vk::Pipeline compositePipeline;
        vk::PipelineLayout compositePipelineLayout;

        vk::DescriptorSetLayout compositeDescriptorSetLayout;
        vk::DescriptorPool compositeDescriptorPool;
        vk::DescriptorSet compositeDescriptorSet;

        vk::Sampler linearSampler;

    public:
       explicit WBOITPipeline(core::Device& device, core::SwapChain& swapChain,
                      core::OffscreenResources& offscreenResources);
        ~WBOITPipeline();

        void init();
        void cleanup();
        void recreate();

        void beginWBOITPass(const vk::CommandBuffer& cmd, uint32_t imageIndex);
        void endWBOITPass(const vk::CommandBuffer& cmd);
        void composite(const vk::CommandBuffer& cmd, uint32_t imageIndex);

        bool isInitialized() const { return initialized; }

    private:
        void createRenderTargets();
        void createSampler();
        void createCompositeDescriptorResources();
        void createCompositePipeline();
        void cleanupRenderTargets();
    };
}
