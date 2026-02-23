#pragma once


#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::vfx
{
    class GPUVFXComputePipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        bool initialized = false;
        bool descriptorsNeedUpdate = true;

        vk::Buffer cachedParticleBuffer;
        vk::Buffer cachedConfigBuffer;
        vk::Buffer cachedStateBuffer;
        vk::Buffer cachedDrawCommandBuffer;
        vk::Buffer cachedLUTBuffer;
        vk::Buffer cachedRibbonRingBuffer;
        vk::Buffer cachedRibbonHeadBuffer;

    public:
        explicit GPUVFXComputePipeline(core::Device& device);
        ~GPUVFXComputePipeline();

        GPUVFXComputePipeline(const GPUVFXComputePipeline&) = delete;
        GPUVFXComputePipeline& operator=(const GPUVFXComputePipeline&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        void updateDescriptors(
            vk::Buffer particleBuffer,
            vk::Buffer configBuffer,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer lutBuffer,
            vk::Buffer ribbonRingBuffer,
            vk::Buffer ribbonHeadBuffer
        );

        void dispatch(
            vk::CommandBuffer cmd,
            uint32_t emitterIndex,
            uint32_t particleCount,
            uint32_t frameNumber,
            uint32_t emitterCount
        );

        void insertBarriersAfterCompute(
            vk::CommandBuffer cmd,
            vk::Buffer particleBuffer,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer ribbonRingBuffer = nullptr,
            vk::Buffer ribbonHeadBuffer = nullptr
        );

        void insertBarriersBeforeCompute(
            vk::CommandBuffer cmd,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer particleBuffer
        );

        void insertBarriersBeforeTransfer(
            vk::CommandBuffer cmd,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer particleBuffer
        );

        void insertTransferToTransferBarrier(
            vk::CommandBuffer cmd,
            vk::Buffer stateBuffer
        );

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void writeDescriptors();
    };
}
