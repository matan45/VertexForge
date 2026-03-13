#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::gpudriven
{
    class GPUCullLODPipeline
    {
    private:
        core::Device& device;

        // Shader
        std::unique_ptr<core::Shader> shader;

        // Pipeline resources
        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        bool initialized = false;
        bool descriptorsNeedUpdate = true;

        // Cached buffer info for descriptor updates
        vk::Buffer cachedObjectBuffer;
        vk::Buffer cachedCameraBuffer;
        vk::Buffer cachedDrawCommandBuffer;
        vk::Buffer cachedPerDrawDataBuffer;
        vk::Buffer cachedDrawCountBuffer;

        // Cached Hi-Z descriptor info
        vk::ImageView cachedHiZView;
        vk::Sampler cachedHiZSampler;
        bool hiZDescriptorNeedsUpdate = false;

    public:
        explicit GPUCullLODPipeline(core::Device& device);
        ~GPUCullLODPipeline();

        // Non-copyable
        GPUCullLODPipeline(const GPUCullLODPipeline&) = delete;
        GPUCullLODPipeline& operator=(const GPUCullLODPipeline&) = delete;

        void init();

        void cleanup();


        void updateDescriptors(
            vk::Buffer objectBuffer,
            vk::Buffer cameraBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer perDrawDataBuffer,
            vk::Buffer drawCountBuffer
        );

        void updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler);

        void dispatch(vk::CommandBuffer cmd, uint32_t objectCount);

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void writeDescriptors();
    };
}
