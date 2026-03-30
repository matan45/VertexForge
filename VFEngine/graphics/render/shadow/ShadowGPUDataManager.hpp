#pragma once

#include "ShadowTypes.hpp"
#include "VSMTypes.hpp"
#include "../../core/RenderManager.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <array>
#include <vector>
#include <unordered_map>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class VSMPhysicalTilePool;

    class ShadowGPUDataManager
    {
    private:
        core::Device& device;

        // Shadow data SSBO (GPUVSMLight array)
        vk::Buffer shadowDataBuffer;
        core::VulkanAllocation shadowDataAllocation;

        struct ShadowStagingFrame
        {
            vk::Buffer buffer;
            core::VulkanAllocation allocation;
            void* mapped = nullptr;
        };

        std::array<ShadowStagingFrame, core::MAX_FRAMES_IN_FLIGHT> stagingFrames{};
        uint32_t currentStagingFrame = 0;

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
            const std::vector<ShadowView>& pointViews,
            const std::vector<ShadowView>& spotViews,
            const std::unordered_map<uint32_t, LightShadowData>& lightShadowData);

        void uploadToGPU(vk::CommandBuffer cmd);
        void advanceStagingFrame() { currentStagingFrame = (currentStagingFrame + 1) % core::MAX_FRAMES_IN_FLIGHT; }

        void updateShadowTextureDescriptor(VSMPhysicalTilePool* tilePool);

        [[nodiscard]] vk::DescriptorSetLayout getShadowDataLayout() const { return shadowDataLayout; }
        [[nodiscard]] vk::DescriptorSet getShadowDataDescSet() const { return shadowDataDescSet; }
        [[nodiscard]] vk::DescriptorSetLayout getShadowTextureLayout() const { return shadowTextureLayout; }
        [[nodiscard]] vk::DescriptorSet getShadowTextureDescSet() const { return shadowTextureDescSet; }

        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] vk::Buffer getShadowDataBuffer() const { return shadowDataBuffer; }

    private:
        void createShadowDataBuffer();
        void destroyShadowDataBuffer();
        void createDescriptorResources();
        void updateDescriptorSet();
        void createShadowTextureDescriptor();
        void destroyShadowTextureDescriptor();
    };
}
