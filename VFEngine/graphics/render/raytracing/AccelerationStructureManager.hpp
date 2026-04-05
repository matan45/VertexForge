#pragma once

#include "ScratchBufferPool.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/DeferredDeletionQueue.hpp"
#include "../../core/GraphicsConstants.hpp"
#include "../gpudriven/GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <array>
#include <mutex>

namespace core
{
    class Device;
}

namespace render::gpudriven
{
    class MergedMeshBuffer;
}

namespace render::raytracing
{
    struct BLASEntry
    {
        vk::AccelerationStructureKHR blas;
        vk::Buffer buffer;
        core::VulkanAllocation allocation;
        vk::DeviceAddress deviceAddress = 0;
        vk::DeviceSize size = 0;
        uint32_t lod0VertexOffset = 0;
        uint32_t lod0IndexOffset = 0;
        uint32_t lod0VertexCount = 0;
        uint32_t lod0IndexCount = 0;
    };

    struct ASMemoryBudget
    {
        vk::DeviceSize blasTotalBytes = 0;
        vk::DeviceSize tlasTotalBytes = 0;
        vk::DeviceSize scratchPeakBytes = 0;
        uint32_t blasCount = 0;
        uint32_t tlasInstanceCount = 0;
    };

    class AccelerationStructureManager
    {
    public:
        explicit AccelerationStructureManager(core::Device& device);
        ~AccelerationStructureManager();

        AccelerationStructureManager(const AccelerationStructureManager&) = delete;
        AccelerationStructureManager& operator=(const AccelerationStructureManager&) = delete;

        void init();
        void cleanup();
        void setDeletionQueue(core::DeferredDeletionQueue* queue) { deletionQueue = queue; }

        void notifyMeshReady(const std::string& meshPath,
                             const std::string& submeshName,
                             uint32_t submeshIndex,
                             const gpudriven::SubmeshLocation& submeshLoc);

        void notifyMeshRemoved(const std::string& meshPath,
                               const std::string& submeshName,
                               uint32_t submeshIndex);

        void buildPendingBLAS(vk::CommandBuffer cmd,
                              vk::Buffer vertexBuffer, uint32_t vertexStride,
                              vk::Buffer indexBuffer);

        void buildTLAS(vk::CommandBuffer cmd,
                       const std::vector<gpudriven::GPUObjectData>& objects,
                       uint32_t objectCount,
                       const gpudriven::MergedMeshBuffer& mergedBuffer);

        // Terrain BLAS support
        void notifyTerrainTileReady(const std::string& tileKey,
                                    uint32_t vertexOffset, uint32_t vertexCount,
                                    uint32_t indexOffset, uint32_t indexCount);
        void notifyTerrainTileRemoved(const std::string& tileKey);
        void buildPendingTerrainBLAS(vk::CommandBuffer cmd,
                                     vk::Buffer terrainVertexBuffer, uint32_t vertexStride,
                                     vk::Buffer terrainIndexBuffer);
        bool hasPendingTerrainBLASBuilds() const { std::lock_guard<std::mutex> lock(pendingMutex); return !pendingTerrainBLASBuilds.empty(); }

        // Extended TLAS build with terrain
        void buildTLASWithTerrain(vk::CommandBuffer cmd,
                                  const std::vector<gpudriven::GPUObjectData>& objects,
                                  uint32_t objectCount,
                                  const gpudriven::MergedMeshBuffer& mergedBuffer,
                                  const std::vector<gpudriven::TerrainTileGPUData>& terrainTiles,
                                  uint32_t terrainTileCount);

        bool isInitialized() const { return initialized; }
        bool isTLASReady() const { return tlasBuilt; }
        bool hasPendingBLASBuilds() const { std::lock_guard<std::mutex> lock(pendingMutex); return !pendingBLASBuilds.empty(); }

        vk::DescriptorSetLayout getTLASDescriptorLayout() const { return tlasDescriptorLayout; }
        vk::DescriptorSet getTLASDescriptorSet() const { return tlasDescriptorSet; }

        const ASMemoryBudget& getMemoryBudget() const { return memoryBudget; }

    private:

        mutable std::mutex pendingMutex;
        core::Device& device;

        // Per-submesh BLAS cache: submeshKey -> BLASEntry
        std::unordered_map<std::string, BLASEntry> blasCache;

        // Pending BLAS builds queued via notifyMeshReady
        struct PendingBLAS
        {
            std::string key;
            uint32_t vertexOffset;
            uint32_t vertexCount;
            uint32_t indexOffset;
            uint32_t indexCount;
        };
        std::vector<PendingBLAS> pendingBLASBuilds;

        // Reverse map: (lod0VertexOffset << 32 | lod0IndexOffset) -> submeshKey
        std::unordered_map<uint64_t, std::string> geometryOffsetToSubmeshKey;

        // Terrain BLAS: tileKey -> BLASEntry
        std::unordered_map<std::string, BLASEntry> terrainBlasCache;
        std::vector<PendingBLAS> pendingTerrainBLASBuilds;
        std::unordered_map<uint64_t, std::string> terrainOffsetToTileKey;

        // TLAS
        vk::AccelerationStructureKHR tlas;
        vk::Buffer tlasBuffer;
        core::VulkanAllocation tlasAllocation;

        // Instance buffer (device-local)
        vk::Buffer instanceBuffer;
        core::VulkanAllocation instanceAllocation;
        uint32_t instanceBufferCapacity = 0;

        // Per-frame staging for instance upload
        struct StagingBuffer
        {
            vk::Buffer buffer;
            core::VulkanAllocation allocation;
            vk::DeviceSize capacity = 0;
        };
        std::array<StagingBuffer, core::MAX_FRAMES_IN_FLIGHT> instanceStagingBuffers{};
        uint32_t currentStagingFrame = 0;

        // Scratch buffers
        ScratchBufferPool blasScratchPool;
        vk::Buffer tlasScratchBuffer;
        core::VulkanAllocation tlasScratchAllocation;
        vk::DeviceSize tlasScratchSize = 0;

        // Descriptor resources
        vk::DescriptorSetLayout tlasDescriptorLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet tlasDescriptorSet;

        uint32_t currentInstanceCount = 0;
        bool tlasBuilt = false;
        bool initialized = false;

        ASMemoryBudget memoryBudget;

        static std::string makeSubmeshKey(const std::string& meshPath,
                                          const std::string& submeshName,
                                          uint32_t submeshIndex);

        static uint64_t makeGeometryOffsetKey(uint32_t vertexOffset, uint32_t indexOffset);

        void destroyBLASEntry(BLASEntry& entry);
        void destroyBLASEntryImmediate(BLASEntry& entry);
        void deferTLASDestruction(vk::AccelerationStructureKHR oldTlas,
                                  vk::Buffer oldBuffer, core::VulkanAllocation oldAlloc);
        void createDescriptorLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptor();
        void ensureInstanceBuffer(vk::DeviceSize requiredSize);
        void ensureStagingBuffer(StagingBuffer& staging, vk::DeviceSize requiredSize);
        void ensureTlasScratch(vk::DeviceSize requiredSize);
        void insertTLASCrossFrameBarrier(vk::CommandBuffer cmd);

        core::DeferredDeletionQueue* deletionQueue = nullptr;
    };
}
