#pragma once

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
        vk::DeviceMemory blasMemory;

        // Top-Level Acceleration Structure (instances)
        vk::AccelerationStructureKHR tlas;
        vk::Buffer tlasBuffer;
        vk::DeviceMemory tlasMemory;

        // Instance buffer for TLAS
        vk::Buffer instanceBuffer;
        vk::DeviceMemory instanceMemory;

        // Scratch buffers
        vk::Buffer blasScratchBuffer;
        vk::DeviceMemory blasScratchMemory;
        vk::Buffer tlasScratchBuffer;
        vk::DeviceMemory tlasScratchMemory;

        // Descriptor for compute shader access
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

        // Build BLAS from merged mesh buffer geometry
        void buildBLAS(vk::CommandBuffer cmd,
                       vk::Buffer vertexBuffer, uint32_t vertexCount, uint32_t vertexStride,
                       vk::Buffer indexBuffer, uint32_t indexCount);

        // Build/update TLAS from object transforms
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

        uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
        void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                          vk::MemoryPropertyFlags properties,
                          vk::Buffer& buffer, vk::DeviceMemory& memory);
        void destroyBuffer(vk::Buffer& buffer, vk::DeviceMemory& memory);
    };
}
