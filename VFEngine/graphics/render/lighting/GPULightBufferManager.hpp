#pragma once

#include "GPULightTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
    class Device;
}

namespace render::lighting
{
    class GPULightBufferManager
    {
    private:
        core::Device& device;

        // Directional light buffers
        vk::Buffer directionalBuffer;
        vk::DeviceMemory directionalMemory;
        vk::Buffer directionalStagingBuffer;
        vk::DeviceMemory directionalStagingMemory;
        void* directionalStagingMapped = nullptr;

        // Point light buffers
        vk::Buffer pointBuffer;
        vk::DeviceMemory pointMemory;
        vk::Buffer pointStagingBuffer;
        vk::DeviceMemory pointStagingMemory;
        void* pointStagingMapped = nullptr;

        // Spot light buffers
        vk::Buffer spotBuffer;
        vk::DeviceMemory spotMemory;
        vk::Buffer spotStagingBuffer;
        vk::DeviceMemory spotStagingMemory;
        void* spotStagingMapped = nullptr;

        // Light counts UBO (HOST_VISIBLE for direct updates)
        vk::Buffer countsBuffer;
        vk::DeviceMemory countsMemory;
        void* countsMapped = nullptr;

        // Descriptor resources
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // CPU-side light data
        std::vector<GPUDirectionalLight> cpuDirectionalLights;
        std::vector<GPUPointLight> cpuPointLights;
        std::vector<GPUSpotLight> cpuSpotLights;

        // Current light counts
        uint32_t directionalCount = 0;
        uint32_t pointCount = 0;
        uint32_t spotCount = 0;

        // State tracking
        bool needsUpload = true;
        bool initialized = false;

    public:
        explicit GPULightBufferManager(core::Device& device);
        ~GPULightBufferManager();

        GPULightBufferManager(const GPULightBufferManager&) = delete;
        GPULightBufferManager& operator=(const GPULightBufferManager&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        // Update light data from ECS scene
        void updateFromScene();

        // Upload staged data to GPU (call within command buffer recording)
        void uploadToGPU(vk::CommandBuffer cmd);

        // Accessors for rendering
        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        uint32_t getDirectionalLightCount() const { return directionalCount; }
        uint32_t getPointLightCount() const { return pointCount; }
        uint32_t getSpotLightCount() const { return spotCount; }

    private:
        void createBuffers();
        void destroyBuffers();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptors();

        void collectDirectionalLights();
        void collectPointLights();
        void collectSpotLights();
        void updateCountsBuffer();
    };
}
