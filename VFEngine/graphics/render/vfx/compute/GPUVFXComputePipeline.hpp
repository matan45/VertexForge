#pragma once

#include "GPUVFXTypes.hpp"
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

        GPUVFXBufferSet cachedBuffers{};

    public:
        explicit GPUVFXComputePipeline(core::Device& device);
        ~GPUVFXComputePipeline();

        GPUVFXComputePipeline(const GPUVFXComputePipeline&) = delete;
        GPUVFXComputePipeline& operator=(const GPUVFXComputePipeline&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        void updateDescriptors(const GPUVFXBufferSet& buffers);

        void dispatch(
            vk::CommandBuffer cmd,
            uint32_t emitterIndex,
            uint32_t particleCount,
            uint32_t frameNumber,
            uint32_t emitterCount,
            uint32_t channelRequestBase = 0,
            uint32_t particlesPerRequest = 0
        );

        void insertBarriersAfterCompute(vk::CommandBuffer cmd, const GPUVFXBufferSet& buffers);

        void insertBarriersBeforeCompute(
            vk::CommandBuffer cmd,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer particleBuffer,
            vk::Buffer eventBuffer = nullptr
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
