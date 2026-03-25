#pragma once

#include "VolumetricTypes.hpp"
#include "../../core/RenderManager.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
    class Device;
}

namespace render::volumetric
{
    class FogVolumeBufferManager
    {
    private:
        core::Device& device;

        // SSBO: [uint32 count, uint32 pad[3], GPUFogVolume volumes[MAX_FOG_VOLUMES]]
        static constexpr vk::DeviceSize HEADER_SIZE = 16; // count + 3 padding uints
        static constexpr vk::DeviceSize BUFFER_SIZE = HEADER_SIZE + MAX_FOG_VOLUMES * sizeof(GPUFogVolume);

        vk::Buffer fogVolumeBuffer;
        vk::DeviceMemory fogVolumeMemory;
        void* mappedMemory = nullptr;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        std::vector<GPUFogVolume> cpuVolumes;
        uint32_t volumeCount = 0;
        bool initialized = false;

    public:
        explicit FogVolumeBufferManager(core::Device& device);
        ~FogVolumeBufferManager();

        FogVolumeBufferManager(const FogVolumeBufferManager&) = delete;
        FogVolumeBufferManager& operator=(const FogVolumeBufferManager&) = delete;

        void init();
        void cleanup();

        void updateFromScene();
        void uploadToGPU();

        [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        [[nodiscard]] vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }
        [[nodiscard]] uint32_t getVolumeCount() const { return volumeCount; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createBuffer();
        void destroyBuffer();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptors();
    };
}
