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
    public:
        explicit GPUVFXComputePipeline(core::Device& device);
        ~GPUVFXComputePipeline();

        GPUVFXComputePipeline(const GPUVFXComputePipeline&) = delete;
        GPUVFXComputePipeline& operator=(const GPUVFXComputePipeline&) = delete;

        // Initialization
        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        // Update descriptor bindings
        void updateDescriptors(
            vk::Buffer particleBuffer,
            vk::Buffer configBuffer,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer
        );

        // Dispatch compute for a single emitter
        void dispatch(
            vk::CommandBuffer cmd,
            uint32_t emitterIndex,
            uint32_t particleCount,
            uint32_t frameNumber,
            uint32_t emitterCount
        );

        // Insert barriers after compute dispatch
        void insertBarriersAfterCompute(
            vk::CommandBuffer cmd,
            vk::Buffer particleBuffer,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer
        );

        // Insert barrier before compute (after state reset and draw command clear)
        void insertBarriersBeforeCompute(
            vk::CommandBuffer cmd,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer particleBuffer
        );

        // Insert barrier before transfer operations (after previous frame's compute)
        void insertBarriersBeforeTransfer(
            vk::CommandBuffer cmd,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer particleBuffer
        );

        // Insert barrier between transfer operations (uploadStateBuffer -> resetAllActiveCounts)
        void insertTransferToTransferBarrier(
            vk::CommandBuffer cmd,
            vk::Buffer stateBuffer
        );

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
        vk::Buffer cachedParticleBuffer;
        vk::Buffer cachedConfigBuffer;
        vk::Buffer cachedStateBuffer;
        vk::Buffer cachedDrawCommandBuffer;

        // Pipeline setup helpers
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void writeDescriptors();
    };
}
