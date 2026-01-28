#pragma once

#include "ShadowTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <unordered_map>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class ShadowAtlasManager;
    class ShadowResourcePool;

    class ShadowGPUDataManager
    {
    private:
        core::Device& device;

        vk::Buffer shadowDataBuffer;
        vk::DeviceMemory shadowDataMemory;
        vk::Buffer shadowDataStagingBuffer;
        vk::DeviceMemory shadowDataStagingMemory;
        void* shadowDataMapped = nullptr;

        vk::DescriptorSetLayout shadowDataLayout;
        vk::DescriptorPool shadowDataPool;
        vk::DescriptorSet shadowDataDescSet;

        vk::DescriptorSetLayout shadowTextureLayout;
        vk::DescriptorPool shadowTexturePool;
        vk::DescriptorSet shadowTextureDescSet;

        std::vector<GPUShadowData> gpuShadowData;

        bool initialized = false;

    public:
        explicit ShadowGPUDataManager(core::Device& device);
        ~ShadowGPUDataManager();

        ShadowGPUDataManager(const ShadowGPUDataManager&) = delete;
        ShadowGPUDataManager& operator=(const ShadowGPUDataManager&) = delete;

        void init();
        void cleanup();

        void buildGPUShadowData(
            const std::vector<ShadowView>& directionalViews,
            const std::vector<ShadowView>& pointViews,
            const std::vector<ShadowView>& spotViews,
            const std::unordered_map<uint32_t, LightShadowData>& lightShadowData,
            const std::unordered_map<uint32_t, uint32_t>& entityToCubeIndex);

        void uploadToGPU(vk::CommandBuffer cmd);

        void updateShadowTextureDescriptor(
            ShadowAtlasManager* atlasManager,
            ShadowResourcePool* resourcePool,
            const std::unordered_map<uint32_t, LightShadowData>& lightShadowData);

        [[nodiscard]] vk::DescriptorSetLayout getShadowDataLayout() const { return shadowDataLayout; }
        [[nodiscard]] vk::DescriptorSet getShadowDataDescSet() const { return shadowDataDescSet; }
        [[nodiscard]] vk::DescriptorSetLayout getShadowTextureLayout() const { return shadowTextureLayout; }
        [[nodiscard]] vk::DescriptorSet getShadowTextureDescSet() const { return shadowTextureDescSet; }

        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] const std::vector<GPUShadowData>& getGPUShadowData() const { return gpuShadowData; }

    private:
        void createShadowDataBuffer();
        void destroyShadowDataBuffer();
        void createDescriptorResources();
        void updateDescriptorSet();
        void createShadowTextureDescriptor();
        void destroyShadowTextureDescriptor();
    };
}
