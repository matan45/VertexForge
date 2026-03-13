#pragma once

#include "../MeshletBufferTypes.hpp"
#include "../FreeListAllocator.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace core
{
    class Device;
    class TransferManager;
}

namespace resource
{
    struct SubmeshMeshletData;
    struct MeshStreamHeader;
}

namespace render::gpudriven
{
    struct LODAllocation
    {
        uint32_t meshletOffset = 0; 
        uint32_t meshletCount = 0; 
        uint32_t vertexOffset = 0; 
        uint32_t vertexCount = 0;
        uint32_t primitiveOffset = 0; 
        uint32_t primitiveCount = 0; 
        bool isAllocated = false;
    };

    struct MeshletAllocation
    {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex;
        
        std::array<LODAllocation, 4> lods{};

        bool hasAnyAllocation() const
        {
            for (const auto& lod : lods)
            {
                if (lod.isAllocated && lod.meshletCount > 0) return true;
            }
            return false;
        }
    };

    class MeshletBuffer
    {
    private:
        core::Device& device;
        std::unique_ptr<core::TransferManager> transferManager;
        
        vk::Buffer meshletBuffer; // GPUMeshlet[] - meshlet descriptors + bounds
        vk::DeviceMemory meshletBufferMemory;

        vk::Buffer meshletVertexBuffer; // uint32_t[] - local to global vertex mapping
        vk::DeviceMemory meshletVertexBufferMemory;

        vk::Buffer meshletPrimitiveBuffer; // uint32_t[] - packed triangle indices
        vk::DeviceMemory meshletPrimitiveBufferMemory;
        
        uint32_t maxMeshletCount = 0;
        uint32_t maxVertexIndexCount = 0;
        uint32_t maxPrimitiveCount = 0;
        
        uint32_t currentMeshletCount = 0;
        uint32_t currentVertexIndexCount = 0;
        uint32_t currentPrimitiveCount = 0;
        
        FreeListAllocator meshletAllocator;
        FreeListAllocator vertexIndexAllocator;
        FreeListAllocator primitiveAllocator;
        
        std::vector<MeshletAllocation> allocations;
        std::unordered_map<std::string, size_t> allocationKeyToIndex;
        std::vector<size_t> freeAllocationSlots; 

        bool initialized = false;

    public:
        explicit MeshletBuffer(core::Device& device);
        ~MeshletBuffer();

        MeshletBuffer(const MeshletBuffer&) = delete;
        MeshletBuffer& operator=(const MeshletBuffer&) = delete;
        
        void init(uint32_t maxMeshlets = MAX_MESHLET_COUNT,
                  uint32_t maxVertexIndices = MAX_MESHLET_VERTEX_INDICES,
                  uint32_t maxPrimitives = MAX_MESHLET_PRIMITIVES);

        void cleanup();
        
        MeshletAllocation* reserveMeshlets(const std::string& meshPath,
                                           const resource::MeshStreamHeader& header);
        
        bool uploadMeshletData(const std::string& meshPath,
                               const std::string& submeshName,
                               uint32_t submeshIndex,
                               uint32_t lodLevel,
                               const resource::SubmeshMeshletData& meshletData,
                               uint32_t baseVertexOffset); 
        
        const MeshletAllocation* getAllocation(const std::string& meshPath,
                                               const std::string& submeshName,
                                               uint32_t submeshIndex) const;
        
        MeshletLODInfo getMeshletLODInfo(const MeshletAllocation& alloc, uint32_t lodLevel) const;
        
        void freeAllMeshlets(const std::string& meshPath);

        vk::Buffer getMeshletBuffer() const { return meshletBuffer; }
        vk::Buffer getMeshletVertexBuffer() const { return meshletVertexBuffer; }
        vk::Buffer getMeshletPrimitiveBuffer() const { return meshletPrimitiveBuffer; }
        
        void flushPendingTransfers();

    private:
        size_t getMeshletBufferSize() const { return maxMeshletCount * sizeof(GPUMeshlet); }
        size_t getMeshletVertexBufferSize() const { return maxVertexIndexCount * sizeof(uint32_t); }
        size_t getMeshletPrimitiveBufferSize() const { return maxPrimitiveCount * sizeof(uint32_t); }
        size_t getTotalBufferSize() const
        {
            return getMeshletBufferSize() + getMeshletVertexBufferSize() + getMeshletPrimitiveBufferSize();
        }

        void createBuffers();
        void destroyBuffers();
        
        void uploadMeshletDataAt(uint32_t offset, const GPUMeshlet* data, uint32_t count);
        void uploadVertexIndicesAt(uint32_t offset, const uint32_t* data, uint32_t count);
        void uploadPrimitivesAt(uint32_t offset, const uint32_t* data, uint32_t count);
        
        bool allocateLODMeshletSpace(LODAllocation& lodAlloc,
                                     uint32_t meshletCount,
                                     uint32_t vertexIndexCount,
                                     uint32_t primitiveCount,
                                     const std::string& debugKey);
        
        void freeLODMeshletSpace(LODAllocation& lodAlloc);

        static std::string makeAllocationKey(const std::string& meshPath,
                                             const std::string& submeshName,
                                             uint32_t submeshIndex)
        {
            return meshPath + ":" + submeshName + "#" + std::to_string(submeshIndex);
        }
    };
}
