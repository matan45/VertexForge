#pragma once

#include "GPULightTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <unordered_set>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class ShadowSystem;
}

namespace render::lighting
{
    class GPULightBufferManager
    {
    private:
        core::Device& device;

        vk::Buffer directionalBuffer;
        vk::DeviceMemory directionalMemory;
        vk::Buffer directionalStagingBuffer;
        vk::DeviceMemory directionalStagingMemory;
        void* directionalStagingMapped = nullptr;

        vk::Buffer pointBuffer;
        vk::DeviceMemory pointMemory;
        vk::Buffer pointStagingBuffer;
        vk::DeviceMemory pointStagingMemory;
        void* pointStagingMapped = nullptr;

        vk::Buffer spotBuffer;
        vk::DeviceMemory spotMemory;
        vk::Buffer spotStagingBuffer;
        vk::DeviceMemory spotStagingMemory;
        void* spotStagingMapped = nullptr;

        vk::Buffer countsBuffer;
        vk::DeviceMemory countsMemory;
        void* countsMapped = nullptr;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        std::vector<GPUDirectionalLight> cpuDirectionalLights;
        std::vector<GPUPointLight> cpuPointLights;
        std::vector<GPUSpotLight> cpuSpotLights;

        uint32_t directionalCount = 0;
        uint32_t pointCount = 0;
        uint32_t spotCount = 0;

        std::vector<GPUDirectionalLight> prevDirectionalLights;
        std::vector<GPUPointLight> prevPointLights;
        std::vector<GPUSpotLight> prevSpotLights;
        uint32_t prevDirectionalCount = 0;
        uint32_t prevPointCount = 0;
        uint32_t prevSpotCount = 0;

        bool needsUpload = true;
        bool initialized = false;

        bool warnedDirectionalLimit = false;
        bool warnedPointLimit = false;
        bool warnedSpotLimit = false;

        shadow::ShadowSystem* shadowSystem = nullptr;
        float shadowIntensity = 0.5f;
        std::unordered_set<uint32_t> registeredShadowLights;

    public:
        explicit GPULightBufferManager(core::Device& device);
        ~GPULightBufferManager();

        GPULightBufferManager(const GPULightBufferManager&) = delete;
        GPULightBufferManager& operator=(const GPULightBufferManager&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        void updateFromScene();
        void updateFromScene(const std::unordered_set<uint32_t>& visibleLightIds);
        void uploadToGPU(vk::CommandBuffer cmd);

        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        uint32_t getDirectionalLightCount() const { return directionalCount; }
        uint32_t getPointLightCount() const { return pointCount; }
        uint32_t getSpotLightCount() const { return spotCount; }

        vk::Buffer getPointBuffer() const { return pointBuffer; }
        vk::Buffer getSpotBuffer() const { return spotBuffer; }

        void setShadowSystem(shadow::ShadowSystem* system) { shadowSystem = system; }
        void setShadowIntensity(float intensity);
        float getShadowIntensity() const { return shadowIntensity; }

    private:
        void createBuffers();
        void destroyBuffers();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptors();

        void collectDirectionalLights(const std::unordered_set<uint32_t>* visibleLightIds = nullptr);
        void collectPointLights(const std::unordered_set<uint32_t>* visibleLightIds = nullptr);
        void collectSpotLights(const std::unordered_set<uint32_t>* visibleLightIds = nullptr);
        void cleanupStaleShadowRegistrations();
        void updateCountsBuffer();
        bool detectChanges();
    };
}
