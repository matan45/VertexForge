#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace core
{
    class Device;
}

namespace render::gpudriven
{
    class MergedMeshBuffer;
    struct GPUObjectData;
}

namespace render::gi
{
    class AccelerationStructureManager
    {
    private:
        core::Device& device;

        // Bottom-Level Acceleration Structure (scene geometry)
        vk::AccelerationStructureKHR blas;
        vk::Buffer blasBuffer;
        core::VulkanAllocation blasAllocation;

        // Top-Level Acceleration Structure (instances)
        vk::AccelerationStructureKHR tlas;
        vk::Buffer tlasBuffer;
        core::VulkanAllocation tlasAllocation;

        vk::Buffer instanceBuffer;
        core::VulkanAllocation instanceAllocation;

        vk::Buffer blasScratchBuffer;
        core::VulkanAllocation blasScratchAllocation;
        vk::Buffer tlasScratchBuffer;
        core::VulkanAllocation tlasScratchAllocation;

        // Staging buffer for TLAS instance upload (kept alive until next frame)
        vk::Buffer tlasStagingBuffer;
        core::VulkanAllocation tlasStagingAllocation;

        vk::DescriptorSetLayout tlasDescriptorLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet tlasDescriptorSet;

        uint32_t currentInstanceCount = 0;
        bool blasBuilt = false;
        bool tlasBuilt = false;
        bool initialized = false;

    public:
        explicit AccelerationStructureManager(core::Device& device);
        ~AccelerationStructureManager();

        AccelerationStructureManager(const AccelerationStructureManager&) = delete;
        AccelerationStructureManager& operator=(const AccelerationStructureManager&) = delete;

        void init();
        void cleanup();

        void buildBLAS(vk::CommandBuffer cmd,
                       vk::Buffer vertexBuffer, uint32_t vertexCount, uint32_t vertexStride,
                       vk::Buffer indexBuffer, uint32_t indexCount);

        void buildTLAS(vk::CommandBuffer cmd,
                       const std::vector<gpudriven::GPUObjectData>& objects,
                       uint32_t objectCount);

        bool isInitialized() const { return initialized; }
        bool isTLASReady() const { return tlasBuilt; }

        vk::DescriptorSetLayout getTLASDescriptorLayout() const { return tlasDescriptorLayout; }
        vk::DescriptorSet getTLASDescriptorSet() const { return tlasDescriptorSet; }

    private:
        void createDescriptorLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptor();

    };
}
