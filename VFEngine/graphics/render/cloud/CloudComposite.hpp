#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    struct OffscreenResources;
}

namespace render::cloud
{
    struct CloudCompositePushConstants
    {
        float nearPlane;
        float farPlane;
        float cloudMinAlt;
        float cloudMaxAlt;
    };

    class CloudComposite
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;

        vk::Sampler sampler;
        vk::ImageView depthOnlyImageView;
        vk::ImageAspectFlags depthAspectMask;

        vk::DescriptorSetLayout dsLayout;
        vk::DescriptorPool dsPool;
        vk::DescriptorSet descriptorSet;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline graphicsPipeline;
        std::shared_ptr<core::Shader> shader;

    public:
        CloudComposite(core::Device& device, core::SwapChain& swapChain,
                       core::OffscreenResources& offscreenResources);
        ~CloudComposite();

        CloudComposite(const CloudComposite&) = delete;
        CloudComposite& operator=(const CloudComposite&) = delete;

        void init(vk::ImageView cloudResultView, vk::Sampler cloudSampler);
        void cleanup();
        void recreate(vk::ImageView cloudResultView, vk::Sampler cloudSampler);

        void render(const vk::CommandBuffer& cmd, uint32_t imageIndex,
                    const CloudCompositePushConstants& pushConstants);

        void renderCompositeGraphManaged(const vk::CommandBuffer& cmd, uint32_t imageIndex,
                                         const CloudCompositePushConstants& pushConstants);

        [[nodiscard]] bool isInitialized() const { return initialized; }
    };
}
