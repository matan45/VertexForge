#pragma once

#include "GPUDrivenTypes.hpp"
#include "MeshletBufferTypes.hpp"
#include "FreeListAllocator.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace core
{
    class Device;
    class TransferManager;
}

namespace resource
{
    struct Vertex;
}

namespace render::gpudriven
{
    // Per-LOD allocation for terrain tile geometry
    struct TerrainLODGeometry
    {
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;

        uint32_t meshletOffset = 0;
        uint32_t meshletCount = 0;
        uint32_t meshletVertexOffset = 0;
        uint32_t meshletVertexCount = 0;
        uint32_t meshletPrimitiveOffset = 0;
        uint32_t meshletPrimitiveCount = 0;

        bool isAllocated = false;
    };

    // Complete allocation for a terrain tile (all 4 LODs)
    struct TerrainTileGeometry
    {
        std::string tileKey;
        std::array<TerrainLODGeometry, LOD_LEVEL_COUNT> lods;
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
        glm::vec4 boundingSphere{0.0f};

        bool hasAnyAllocation() const
        {
            for (const auto& lod : lods)
            {
                if (lod.isAllocated) return true;
            }
            return false;
        }
    };

    // Dedicated buffer for terrain mesh data (vertices, indices, meshlets)
    // Separate from MergedMeshBuffer to avoid fragmentation with regular meshes
    //
    // SYNCHRONIZATION CONTRACT:
    // Upload methods (uploadLODVertices, uploadLODIndices, uploadLODMeshlets) use async transfers.
    // Callers MUST call flushPendingTransfers() before using the buffers for rendering.
    // GPUDrivenRenderer::dispatchCompute() handles this automatically before terrain rendering.
    class TerrainMeshBuffer
    {
    private:
        core::Device& device_;
        std::unique_ptr<core::TransferManager> transferManager_;

        // Vertex/Index buffers
        vk::Buffer vertexBuffer_;
        vk::DeviceMemory vertexBufferMemory_;
        vk::Buffer indexBuffer_;
        vk::DeviceMemory indexBufferMemory_;

        // Meshlet buffers
        vk::Buffer meshletBuffer_;
        vk::DeviceMemory meshletBufferMemory_;
        vk::Buffer meshletVertexBuffer_;
        vk::DeviceMemory meshletVertexBufferMemory_;
        vk::Buffer meshletPrimitiveBuffer_;
        vk::DeviceMemory meshletPrimitiveBufferMemory_;

        // Capacity limits
        uint32_t maxVertexCount_ = 0;
        uint32_t maxIndexCount_ = 0;
        uint32_t maxMeshletCount_ = 0;
        uint32_t maxMeshletVertexCount_ = 0;
        uint32_t maxMeshletPrimitiveCount_ = 0;

        // Current usage
        uint32_t currentVertexCount_ = 0;
        uint32_t currentIndexCount_ = 0;
        uint32_t currentMeshletCount_ = 0;
        uint32_t currentMeshletVertexCount_ = 0;
        uint32_t currentMeshletPrimitiveCount_ = 0;

        // Free list allocators
        FreeListAllocator vertexAllocator_;
        FreeListAllocator indexAllocator_;
        FreeListAllocator meshletAllocator_;
        FreeListAllocator meshletVertexAllocator_;
        FreeListAllocator meshletPrimitiveAllocator_;

        // Tile allocations
        std::unordered_map<std::string, TerrainTileGeometry> tileAllocations_;

        bool initialized_ = false;

        static constexpr uint32_t VERTEX_STRIDE = 64; // sizeof(resource::Vertex)

    public:
        explicit TerrainMeshBuffer(core::Device& device);
        ~TerrainMeshBuffer();

        TerrainMeshBuffer(const TerrainMeshBuffer&) = delete;
        TerrainMeshBuffer& operator=(const TerrainMeshBuffer&) = delete;

        // Initialize with capacity for terrain
        // Default: 15M vertices (~960MB), 60M indices (~240MB), 1.5M meshlets (~48MB)
        // Total ~1.5GB GPU memory - suitable for medium terrain scenes
        // Note: For very large terrains, per-LOD streaming should upload only needed LOD
        void init(uint32_t maxVertices = 15000000,
                  uint32_t maxIndices = 60000000,
                  uint32_t maxMeshlets = 1500000,
                  uint32_t maxMeshletVertices = 30000000,
                  uint32_t maxMeshletPrimitives = 30000000);

        void cleanup();

        // Allocate space for a terrain tile (all 4 LODs)
        TerrainTileGeometry* allocateTile(
            const std::string& tileKey,
            const std::array<uint32_t, LOD_LEVEL_COUNT>& vertexCounts,
            const std::array<uint32_t, LOD_LEVEL_COUNT>& indexCounts,
            const std::array<uint32_t, LOD_LEVEL_COUNT>& meshletCounts,
            const std::array<uint32_t, LOD_LEVEL_COUNT>& meshletVertexCounts,
            const std::array<uint32_t, LOD_LEVEL_COUNT>& meshletPrimitiveCounts,
            const glm::vec3& aabbMin,
            const glm::vec3& aabbMax);

        // Upload geometry data for a specific LOD
        bool uploadLODVertices(const std::string& tileKey, uint32_t lodLevel,
                               const resource::Vertex* vertices, uint32_t count);
        bool uploadLODIndices(const std::string& tileKey, uint32_t lodLevel,
                              const uint32_t* indices, uint32_t count);
        bool uploadLODMeshlets(const std::string& tileKey, uint32_t lodLevel,
                               const GPUMeshlet* meshlets, uint32_t count,
                               const uint32_t* meshletVertices, uint32_t meshletVertexCount,
                               const uint32_t* meshletPrimitives, uint32_t meshletPrimitiveCount);

        // Free a tile's allocation
        void freeTile(const std::string& tileKey);

        // Allocate space for a single LOD on an existing tile
        // Creates tile if it doesn't exist
        bool allocateTileLOD(const std::string& tileKey,
                             uint32_t lodLevel,
                             uint32_t vertexCount,
                             uint32_t indexCount,
                             uint32_t meshletCount,
                             uint32_t meshletVertexCount,
                             uint32_t meshletPrimitiveCount,
                             const glm::vec3& aabbMin,
                             const glm::vec3& aabbMax);

        // Free a single LOD from a tile
        void freeTileLOD(const std::string& tileKey, uint32_t lodLevel);

        // Check if tile is allocated
        bool hasTile(const std::string& tileKey) const;

        // Check if specific LOD is allocated
        bool hasTileLOD(const std::string& tileKey, uint32_t lodLevel) const;

        // Get tile allocation
        const TerrainTileGeometry* getTileGeometry(const std::string& tileKey) const;
        TerrainTileGeometry* getTileGeometryMutable(const std::string& tileKey);

        // Flush pending async transfers - MUST be called before rendering
        // Blocks until all queued transfers complete
        void flushPendingTransfers();

        // Clear all allocations
        void clear();

        // Buffer accessors
        vk::Buffer getVertexBuffer() const { return vertexBuffer_; }
        vk::Buffer getIndexBuffer() const { return indexBuffer_; }
        vk::Buffer getMeshletBuffer() const { return meshletBuffer_; }
        vk::Buffer getMeshletVertexBuffer() const { return meshletVertexBuffer_; }
        vk::Buffer getMeshletPrimitiveBuffer() const { return meshletPrimitiveBuffer_; }

        // Size accessors
        size_t getVertexBufferSize() const { return maxVertexCount_ * VERTEX_STRIDE; }
        size_t getIndexBufferSize() const { return maxIndexCount_ * sizeof(uint32_t); }
        size_t getMeshletBufferSize() const { return maxMeshletCount_ * sizeof(GPUMeshlet); }
        size_t getTotalBufferSize() const;

        // Usage stats
        uint32_t getAllocatedTileCount() const { return static_cast<uint32_t>(tileAllocations_.size()); }
        uint32_t getCurrentVertexCount() const { return currentVertexCount_; }
        uint32_t getCurrentIndexCount() const { return currentIndexCount_; }
        uint32_t getCurrentMeshletCount() const { return currentMeshletCount_; }

        bool isInitialized() const { return initialized_; }

    private:
        void createBuffers();
        void destroyBuffers();

        void uploadVertexDataAt(uint32_t offset, const void* data, uint32_t vertexCount);
        void uploadIndexDataAt(uint32_t offset, const uint32_t* data, uint32_t indexCount);
        void uploadMeshletDataAt(uint32_t offset, const GPUMeshlet* data, uint32_t count);
        void uploadMeshletVerticesAt(uint32_t offset, const uint32_t* data, uint32_t count);
        void uploadMeshletPrimitivesAt(uint32_t offset, const uint32_t* data, uint32_t count);

        bool allocateLODSpace(TerrainLODGeometry& lod,
                              uint32_t vertexCount, uint32_t indexCount,
                              uint32_t meshletCount, uint32_t meshletVertexCount,
                              uint32_t meshletPrimitiveCount,
                              const std::string& debugKey);

        void freeLODSpace(TerrainLODGeometry& lod);
    };
}
