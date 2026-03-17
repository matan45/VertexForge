#pragma once

#include "ShadowTypes.hpp"
#include "VSMTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <unordered_map>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class VSMPhysicalTilePool;
    class ShadowResourcePool;

    class ShadowGPUDataManager
    {
    private:
        core::Device& device;

        // Shadow data SSBO (GPUVSMLight array)
        vk::Buffer shadowDataBuffer;
        vk::DeviceMemory shadowDataMemory;
        vk::Buffer shadowDataStagingBuffer;
        vk::DeviceMemory shadowDataStagingMemory;
        void* shadowDataMapped = nullptr;

        // Page table SSBO descriptor (actual buffer owned by VSMPageTable)
        vk::DescriptorSetLayout shadowDataLayout;  // set 9: binding 0 = VSMLight[], binding 1 = pageTable[]
        vk::DescriptorPool shadowDataPool;
        vk::DescriptorSet shadowDataDescSet;

        vk::DescriptorSetLayout shadowTextureLayout; // set 10
        vk::DescriptorPool shadowTexturePool;
        vk::DescriptorSet shadowTextureDescSet;

        std::vector<vsm::GPUVSMLight> gpuShadowData;

        // Reference to page table buffer for descriptor binding
        vk::Buffer pageTableBufferRef;
        vk::DeviceSize pageTableBufferSize = 0;

        bool initialized = false;

    public:
        explicit ShadowGPUDataManager(core::Device& device);
        ~ShadowGPUDataManager();

        ShadowGPUDataManager(const ShadowGPUDataManager&) = delete;
        ShadowGPUDataManager& operator=(const ShadowGPUDataManager&) = delete;

        void init();
        void cleanup();

        void setPageTableBuffer(vk::Buffer buffer, vk::DeviceSize size);

        void buildGPUShadowData(
            const std::vector<ShadowView>& directionalViews,
            const std::vector<ShadowView>& pointViews,
            const std::vector<ShadowView>& spotViews,
            const std::unordered_map<uint32_t, LightShadowData>& lightShadowData,
            const std::unordered_map<uint32_t, uint32_t>& entityToCubeIndex);

        void uploadToGPU(vk::CommandBuffer cmd);

        void updateShadowTextureDescriptor(
            VSMPhysicalTilePool* tilePool,
            ShadowResourcePool* resourcePool,
            const std::unordered_map<uint32_t, LightShadowData>& lightShadowData);

        [[nodiscard]] vk::DescriptorSetLayout getShadowDataLayout() const { return shadowDataLayout; }
        [[nodiscard]] vk::DescriptorSet getShadowDataDescSet() const { return shadowDataDescSet; }
        [[nodiscard]] vk::DescriptorSetLayout getShadowTextureLayout() const { return shadowTextureLayout; }
        [[nodiscard]] vk::DescriptorSet getShadowTextureDescSet() const { return shadowTextureDescSet; }

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createShadowDataBuffer();
        void destroyShadowDataBuffer();
        void createDescriptorResources();
        void updateDescriptorSet();
        void createShadowTextureDescriptor();
        void destroyShadowTextureDescriptor();
    };
}
