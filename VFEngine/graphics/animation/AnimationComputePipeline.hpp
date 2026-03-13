#pragma once

#include "AnimationGPUData.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class Shader;
}

namespace animation
{
    class AnimationComputePipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        vk::Buffer requestBuffer;
        vk::DeviceMemory requestBufferMemory;
        void* requestMapped = nullptr;

        vk::Buffer skeletonBuffer;
        vk::DeviceMemory skeletonBufferMemory;

        vk::Buffer clipHeaderBuffer;
        vk::DeviceMemory clipHeaderBufferMemory;

        vk::Buffer channelHeaderBuffer;
        vk::DeviceMemory channelHeaderBufferMemory;

        vk::Buffer positionKeyBuffer;
        vk::DeviceMemory positionKeyBufferMemory;

        vk::Buffer rotationKeyBuffer;
        vk::DeviceMemory rotationKeyBufferMemory;

        vk::Buffer scaleKeyBuffer;
        vk::DeviceMemory scaleKeyBufferMemory;

        bool initialized = false;
        bool descriptorsNeedUpdate = true;
        uint32_t maxEntities = 0;

    public:
        explicit AnimationComputePipeline(core::Device& device);
        ~AnimationComputePipeline();

        AnimationComputePipeline(const AnimationComputePipeline&) = delete;
        AnimationComputePipeline& operator=(const AnimationComputePipeline&) = delete;

        void init(uint32_t maxAnimatedEntities = 4096);
        void cleanup();
        bool isInitialized() const { return initialized; }

        void uploadAnimationData(const AnimationGPUUploadData& data);
        void updateRequests(const std::vector<GPUAnimEvalRequest>& requests);
        void dispatch(vk::CommandBuffer cmd, uint32_t entityCount, vk::Buffer outputBoneBuffer);
        void insertBarriersAfterDispatch(vk::CommandBuffer cmd, vk::Buffer outputBoneBuffer);
    };
}
