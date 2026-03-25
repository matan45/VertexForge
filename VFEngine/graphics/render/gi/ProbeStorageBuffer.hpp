#pragma once

#include "GITypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
    class Device;
}

namespace render::gi
{
    class ProbeStorageBuffer
    {
    private:
        core::Device& device;

        // Ping-pong probe data SSBOs (frame N-1 read, frame N write)
        vk::Buffer probeBufferA;
        vk::DeviceMemory probeMemoryA;
        vk::Buffer probeBufferB;
        vk::DeviceMemory probeMemoryB;

        vk::Buffer probeStagingBuffer;
        vk::DeviceMemory probeStagingMemory;
        void* probeStagingMapped = nullptr;

        vk::Buffer cascadeInfoBuffer;
        vk::DeviceMemory cascadeInfoMemory;
        void* cascadeInfoMapped = nullptr;

        vk::DescriptorSetLayout probeDataLayout;
        vk::DescriptorSetLayout cascadeInfoLayout;
        vk::DescriptorSetLayout samplingLayout;  // Combined layout for fragment shader GI sampling
        vk::DescriptorSetLayout computeSamplingLayout; // Combined layout for compute shader GI sampling
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet probeDataDescSetA;  // Read from A
        vk::DescriptorSet probeDataDescSetB;  // Read from B
        vk::DescriptorSet cascadeInfoDescSet;
        vk::DescriptorSet samplingDescSetA;   // Fragment sampling: read A + cascade
        vk::DescriptorSet samplingDescSetB;   // Fragment sampling: read B + cascade
        vk::DescriptorSet computeSamplingDescSetA; // Compute sampling: read A + cascade
        vk::DescriptorSet computeSamplingDescSetB; // Compute sampling: read B + cascade

        uint32_t probeCount = 0;
        uint32_t cascadeCount = 0;
        uint32_t currentReadBuffer = 0; // 0 = A, 1 = B
        bool initialized = false;

    public:
        explicit ProbeStorageBuffer(core::Device& device);
        ~ProbeStorageBuffer();

        ProbeStorageBuffer(const ProbeStorageBuffer&) = delete;
        ProbeStorageBuffer& operator=(const ProbeStorageBuffer&) = delete;

        void init(uint32_t maxProbes, uint32_t maxCascades);
        void cleanup();

        void uploadCascadeInfo(const std::vector<CascadeLevel>& cascades);
        void uploadToGPU(vk::CommandBuffer cmd);

        void swapBuffers();

        vk::Buffer getReadBuffer() const { return currentReadBuffer == 0 ? probeBufferA : probeBufferB; }
        vk::Buffer getWriteBuffer() const { return currentReadBuffer == 0 ? probeBufferB : probeBufferA; }

        vk::DescriptorSetLayout getProbeDataLayout() const { return probeDataLayout; }
        vk::DescriptorSet getProbeDataDescSet() const
        {
            return currentReadBuffer == 0 ? probeDataDescSetA : probeDataDescSetB;
        }
        vk::DescriptorSet getProbeWriteDescSet() const
        {
            return currentReadBuffer == 0 ? probeDataDescSetB : probeDataDescSetA;
        }

        vk::DescriptorSetLayout getCascadeInfoLayout() const { return cascadeInfoLayout; }
        vk::DescriptorSet getCascadeInfoDescSet() const { return cascadeInfoDescSet; }

        // Combined sampling layout/set for fragment shaders (set 11)
        vk::DescriptorSetLayout getSamplingLayout() const { return samplingLayout; }
        vk::DescriptorSet getSamplingDescSet() const
        {
            return currentReadBuffer == 0 ? samplingDescSetA : samplingDescSetB;
        }

        // Combined sampling layout/set for compute shaders (volumetric fog GI injection)
        vk::DescriptorSetLayout getComputeSamplingLayout() const { return computeSamplingLayout; }
        vk::DescriptorSet getComputeSamplingDescSet() const
        {
            return currentReadBuffer == 0 ? computeSamplingDescSetA : computeSamplingDescSetB;
        }

        uint32_t getProbeCount() const { return probeCount; }
        bool isInitialized() const { return initialized; }

    private:
        void createBuffers();
        void createDescriptorLayouts();
        void createDescriptorPool();
        void allocateDescriptorSets();
        void updateDescriptors();
        void destroyBuffers();
    };
}
