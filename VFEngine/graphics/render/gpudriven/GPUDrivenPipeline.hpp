#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::gpudriven
{
    class GPUDrivenPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<core::Shader> meshShader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout perDrawDataLayout;
        vk::DescriptorPool perDrawDataPool;
        vk::DescriptorSet perDrawDataDescriptorSet;

    public:
        explicit GPUDrivenPipeline(core::Device& device, core::SwapChain& swapChain);
        ~GPUDrivenPipeline();

        GPUDrivenPipeline(const GPUDrivenPipeline&) = delete;
        GPUDrivenPipeline& operator=(const GPUDrivenPipeline&) = delete;

        void init(vk::DescriptorSetLayout iblLayout,
                  vk::DescriptorSetLayout bindlessTextureLayout,
                  vk::RenderPass renderPass);

        void cleanup();

        void recreate(vk::DescriptorSetLayout iblLayout,
                      vk::DescriptorSetLayout bindlessTextureLayout,
                      vk::RenderPass renderPass);

        void updatePerDrawDescriptor(vk::Buffer perDrawDataBuffer);

        vk::Pipeline getPipeline() const { return graphicsPipeline; }
        vk::PipelineLayout getPipelineLayout() const { return pipelineLayout; }
        vk::DescriptorSetLayout getPerDrawDataLayout() const { return perDrawDataLayout; }
        vk::DescriptorSet getPerDrawDataDescriptorSet() const { return perDrawDataDescriptorSet; }

    private:
        void createPerDrawDataDescriptor();
        void createGraphicsPipeline(vk::DescriptorSetLayout iblLayout,
                                    vk::DescriptorSetLayout bindlessTextureLayout,
                                    vk::RenderPass renderPass);
    };
}
