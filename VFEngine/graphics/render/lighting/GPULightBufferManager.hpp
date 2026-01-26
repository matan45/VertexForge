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

        // Previous frame data for dirty detection
        std::vector<GPUDirectionalLight> prevDirectionalLights;
        std::vector<GPUPointLight> prevPointLights;
        std::vector<GPUSpotLight> prevSpotLights;
        uint32_t prevDirectionalCount = 0;
        uint32_t prevPointCount = 0;
        uint32_t prevSpotCount = 0;

        // State tracking
        bool needsUpload = true;
        bool initialized = false;

        // Warning flags to prevent log spam (reset when count drops below limit)
        bool warnedDirectionalLimit = false;
        bool warnedPointLimit = false;
        bool warnedSpotLimit = false;

        // Shadow system reference for shadow index lookup (optional)
        shadow::ShadowSystem* shadowSystem = nullptr;

        // Track which lights have been registered for shadows (by entity ID)
        std::unordered_set<uint32_t> registeredShadowLights;

    public:
        explicit GPULightBufferManager(core::Device& device);
        ~GPULightBufferManager();

        GPULightBufferManager(const GPULightBufferManager&) = delete;
        GPULightBufferManager& operator=(const GPULightBufferManager&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        // Update light data from ECS scene (collects ALL lights - no culling)
        void updateFromScene();

        // Update light data with BVH pre-culling (only collects visible lights)
        // Pass the set of entity IDs returned from LightBVH::queryFrustum()
        void updateFromScene(const std::unordered_set<uint32_t>& visibleLightIds);

        // Upload staged data to GPU (call within command buffer recording)
        void uploadToGPU(vk::CommandBuffer cmd);

        // Accessors for rendering
        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        uint32_t getDirectionalLightCount() const { return directionalCount; }
        uint32_t getPointLightCount() const { return pointCount; }
        uint32_t getSpotLightCount() const { return spotCount; }

        vk::Buffer getPointBuffer() const { return pointBuffer; }
        vk::Buffer getSpotBuffer() const { return spotBuffer; }

        // Set shadow system reference for shadow index population
        void setShadowSystem(shadow::ShadowSystem* system) { shadowSystem = system; }

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
        void updateCountsBuffer();
        bool detectChanges();
    };
}
