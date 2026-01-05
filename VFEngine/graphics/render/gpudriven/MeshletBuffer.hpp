#pragma once

#include "MeshletBufferTypes.hpp"
#include "FreeListAllocator.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace core {
    class Device;
    class TransferManager;
}

namespace resource {
    struct SubmeshMeshletData;
    struct MeshStreamHeader;
}

namespace render::gpudriven {

    // Tracking info for registered meshlet data
    struct MeshletAllocation {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex;

        // Per-LOD allocation info
        struct LODAllocation {
            uint32_t meshletOffset = 0;      // Offset into meshlet buffer
            uint32_t meshletCount = 0;       // Number of meshlets
            uint32_t vertexOffset = 0;       // Offset into meshlet vertex buffer
            uint32_t vertexCount = 0;        // Number of vertex indices
            uint32_t primitiveOffset = 0;    // Offset into meshlet primitive buffer
            uint32_t primitiveCount = 0;     // Number of primitives
            bool isAllocated = false;
        };
        std::array<LODAllocation, 4> lods{};

        bool hasAnyAllocation() const {
            for (const auto& lod : lods) {
                if (lod.isAllocated && lod.meshletCount > 0) return true;
            }
            return false;
        }
    };

    // Statistics for meshlet buffer
    struct MeshletBufferStats {
        uint32_t totalMeshlets = 0;
        uint32_t usedMeshlets = 0;
        uint32_t totalVertexIndices = 0;
        uint32_t usedVertexIndices = 0;
        uint32_t totalPrimitives = 0;
        uint32_t usedPrimitives = 0;
        size_t totalMemoryBytes = 0;
        size_t usedMemoryBytes = 0;
    };

    class MeshletBuffer {
    private:
        core::Device& device;
        std::unique_ptr<core::TransferManager> transferManager;

        // GPU Buffers
        vk::Buffer meshletBuffer;           // GPUMeshlet[] - meshlet descriptors + bounds
        vk::DeviceMemory meshletBufferMemory;

        vk::Buffer meshletVertexBuffer;     // uint32_t[] - local to global vertex mapping
        vk::DeviceMemory meshletVertexBufferMemory;

        vk::Buffer meshletPrimitiveBuffer;  // uint32_t[] - packed triangle indices
        vk::DeviceMemory meshletPrimitiveBufferMemory;

        // Buffer capacities
        uint32_t maxMeshletCount = 0;
        uint32_t maxVertexIndexCount = 0;
        uint32_t maxPrimitiveCount = 0;

        // Current usage
        uint32_t currentMeshletCount = 0;
        uint32_t currentVertexIndexCount = 0;
        uint32_t currentPrimitiveCount = 0;

        // Free-list allocators for space management
        FreeListAllocator meshletAllocator;
        FreeListAllocator vertexIndexAllocator;
        FreeListAllocator primitiveAllocator;

        // Registered meshlet allocations
        std::vector<MeshletAllocation> allocations;
        std::unordered_map<std::string, size_t> allocationKeyToIndex;
        std::vector<size_t> freeAllocationSlots;  // Reusable slots in allocations vector

        bool initialized = false;

    public:
        explicit MeshletBuffer(core::Device& device);
        ~MeshletBuffer();

        MeshletBuffer(const MeshletBuffer&) = delete;
        MeshletBuffer& operator=(const MeshletBuffer&) = delete;

        // Initialize buffers with given capacities
        void init(uint32_t maxMeshlets = MAX_MESHLET_COUNT,
                  uint32_t maxVertexIndices = MAX_MESHLET_VERTEX_INDICES,
                  uint32_t maxPrimitives = MAX_MESHLET_PRIMITIVES);

        void cleanup();

        // Reserve space for a mesh's meshlet data based on header info
        MeshletAllocation* reserveMeshlets(const std::string& meshPath,
                                            const resource::MeshStreamHeader& header);

        // Upload meshlet data for a submesh
        bool uploadMeshletData(const std::string& meshPath,
                               const std::string& submeshName,
                               uint32_t submeshIndex,
                               uint32_t lodLevel,
                               const resource::SubmeshMeshletData& meshletData,
                               uint32_t baseVertexOffset);  // Offset into merged vertex buffer

        // Unregister mesh and free its allocations
        void unregisterMesh(const std::string& meshPath);

        // Get allocation info for a submesh
        const MeshletAllocation* getAllocation(const std::string& meshPath,
                                                const std::string& submeshName,
                                                uint32_t submeshIndex) const;

        MeshletAllocation* getAllocationMutable(const std::string& meshPath,
                                                 const std::string& submeshName,
                                                 uint32_t submeshIndex);

        // Get MeshletLODInfo for populating SubmeshLocation
        MeshletLODInfo getMeshletLODInfo(const MeshletAllocation& alloc, uint32_t lodLevel) const;

        // Accessors
        vk::Buffer getMeshletBuffer() const { return meshletBuffer; }
        vk::Buffer getMeshletVertexBuffer() const { return meshletVertexBuffer; }
        vk::Buffer getMeshletPrimitiveBuffer() const { return meshletPrimitiveBuffer; }

        uint32_t getMeshletCount() const { return currentMeshletCount; }
        uint32_t getVertexIndexCount() const { return currentVertexIndexCount; }
        uint32_t getPrimitiveCount() const { return currentPrimitiveCount; }

        bool isInitialized() const { return initialized; }

        // Statistics
        MeshletBufferStats getStats() const;

        // Buffer sizes in bytes
        size_t getMeshletBufferSize() const { return maxMeshletCount * sizeof(GPUMeshlet); }
        size_t getMeshletVertexBufferSize() const { return maxVertexIndexCount * sizeof(uint32_t); }
        size_t getMeshletPrimitiveBufferSize() const { return maxPrimitiveCount * sizeof(uint32_t); }
        size_t getTotalBufferSize() const {
            return getMeshletBufferSize() + getMeshletVertexBufferSize() + getMeshletPrimitiveBufferSize();
        }

    private:
        void createBuffers();
        void destroyBuffers();

        // Upload data to specific buffer offsets
        void uploadMeshletDataAt(uint32_t offset, const GPUMeshlet* data, uint32_t count);
        void uploadVertexIndicesAt(uint32_t offset, const uint32_t* data, uint32_t count);
        void uploadPrimitivesAt(uint32_t offset, const uint32_t* data, uint32_t count);

        // Allocate space for a LOD's meshlet data
        bool allocateLODMeshletSpace(MeshletAllocation::LODAllocation& lodAlloc,
                                      uint32_t meshletCount,
                                      uint32_t vertexIndexCount,
                                      uint32_t primitiveCount,
                                      const std::string& debugKey);

        // Free space for a LOD
        void freeLODMeshletSpace(MeshletAllocation::LODAllocation& lodAlloc);

        static std::string makeAllocationKey(const std::string& meshPath,
                                              const std::string& submeshName,
                                              uint32_t submeshIndex) {
            return meshPath + ":" + submeshName + "#" + std::to_string(submeshIndex);
        }
    };

}
