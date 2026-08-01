#pragma once

#include "../GPUDrivenTypes.hpp"
#include "../MeshletBufferTypes.hpp"
#include "../FreeListAllocator.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
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

    struct TerrainTileGeometry
    {
        std::string tileKey;
        std::array<TerrainLODGeometry, TERRAIN_LOD_LEVEL_COUNT> lods;
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
        glm::vec4 boundingSphere{0.0f};

        uint32_t weightMapOffset = 0;   // Offset in uint32 elements
        uint32_t weightMapSize = 0;     // Size in uint32 elements
        bool weightMapAllocated = false;

        // VK-1620: the tile's terrain heights, as a (vertexCount x vertexCount) row-major float
        // grid — the SAME grid the weight map uses (TerrainTile::initializeWeightMap sizes it from
        // config.getVertexCount(), which is also heightData's stride), so one resolution serves
        // both and the bake's height sample lands on exactly the texel its splat sample did.
        //
        // Deliberately NOT read out of the terrain vertex buffer, which would be free: only the
        // COARSEST LOD is guaranteed resident (TerrainStreamManager::FALLBACK_LOD), and
        // populateGPUTile writes lodNMeshletData for every LOD whether allocated or not, so a
        // vertex read on a non-resident LOD 0 silently returns whatever tile owns vertex slot 0.
        uint32_t heightFieldOffset = 0; // Offset in float elements
        uint32_t heightFieldSize = 0;   // Size in float elements
        bool heightFieldAllocated = false;

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

        vk::Buffer vertexBuffer_;
        core::VulkanAllocation vertexBufferAllocation_;
        vk::Buffer indexBuffer_;
        core::VulkanAllocation indexBufferAllocation_;

        vk::Buffer meshletBuffer_;
        core::VulkanAllocation meshletBufferAllocation_;
        vk::Buffer meshletVertexBuffer_;
        core::VulkanAllocation meshletVertexBufferAllocation_;
        vk::Buffer meshletPrimitiveBuffer_;
        core::VulkanAllocation meshletPrimitiveBufferAllocation_;

        vk::Buffer weightMapBuffer_;
        core::VulkanAllocation weightMapBufferAllocation_;

        // VK-1620 per-tile terrain heights. Null unless the world-height plane is enabled.
        vk::Buffer heightFieldBuffer_;
        core::VulkanAllocation heightFieldBufferAllocation_;

        uint32_t maxVertexCount_ = 0;
        uint32_t maxIndexCount_ = 0;
        uint32_t maxMeshletCount_ = 0;
        uint32_t maxMeshletVertexCount_ = 0;
        uint32_t maxMeshletPrimitiveCount_ = 0;

        uint32_t maxWeightMapElements_ = 0;     // In uint32 elements (4 bytes each)
        FreeListAllocator weightMapAllocator_;

        // VK-1620. Half the weight arena's element count for the same tile capacity: a tile spends
        // 8 bytes/texel on splat weights but only 4 on a height.
        uint32_t maxHeightFieldElements_ = 0;   // In float elements (4 bytes each)
        FreeListAllocator heightFieldAllocator_;
        bool heightFieldEnabled_ = false;

        FreeListAllocator vertexAllocator_;
        FreeListAllocator indexAllocator_;
        FreeListAllocator meshletAllocator_;
        FreeListAllocator meshletVertexAllocator_;
        FreeListAllocator meshletPrimitiveAllocator_;

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
        //
        // VK-1620: `worldHeightField` creates the per-tile heightfield arena the RVT bake samples
        // to write its world-height plane. It is a separate 64 MB buffer and is only worth paying
        // for when that plane exists, so it is a required argument rather than a default - the
        // caller has already resolved the flag and there must be exactly one answer.
        void init(bool worldHeightField,
                  uint32_t maxVertices = 15000000,
                  uint32_t maxIndices = 60000000,
                  uint32_t maxMeshlets = 1500000,
                  uint32_t maxMeshletVertices = 30000000,
                  uint32_t maxMeshletPrimitives = 30000000);

        void cleanup();

        TerrainTileGeometry* allocateTile(
            const std::string& tileKey,
            const std::array<uint32_t, TERRAIN_LOD_LEVEL_COUNT>& vertexCounts,
            const std::array<uint32_t, TERRAIN_LOD_LEVEL_COUNT>& indexCounts,
            const std::array<uint32_t, TERRAIN_LOD_LEVEL_COUNT>& meshletCounts,
            const std::array<uint32_t, TERRAIN_LOD_LEVEL_COUNT>& meshletVertexCounts,
            const std::array<uint32_t, TERRAIN_LOD_LEVEL_COUNT>& meshletPrimitiveCounts,
            const glm::vec3& aabbMin,
            const glm::vec3& aabbMax);

        bool uploadLODVertices(const std::string& tileKey, uint32_t lodLevel,
                               const resource::Vertex* vertices, uint32_t count);
        bool uploadLODIndices(const std::string& tileKey, uint32_t lodLevel,
                              const uint32_t* indices, uint32_t count);
        bool uploadLODMeshlets(const std::string& tileKey, uint32_t lodLevel,
                               const GPUMeshlet* meshlets, uint32_t count,
                               const uint32_t* meshletVertices, uint32_t meshletVertexCount,
                               const uint32_t* meshletPrimitives, uint32_t meshletPrimitiveCount);

        void freeTile(const std::string& tileKey);

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

        void freeTileLOD(const std::string& tileKey, uint32_t lodLevel);

        const TerrainTileGeometry* getTileGeometry(const std::string& tileKey) const;

        // MUST be called before rendering
        void flushPendingTransfers();

        void clear();

        // Weight map buffer management
        uint32_t allocateWeightMap(const std::string& tileKey, uint32_t sizeBytes);
        bool uploadWeightMapData(const std::string& tileKey, const void* data, uint32_t sizeBytes);

        // VK-1620 heightfield buffer management. Both no-op and report failure when the world-height
        // plane is off, so callers need no separate gate.
        uint32_t allocateHeightField(const std::string& tileKey, uint32_t sizeBytes);
        bool uploadHeightFieldData(const std::string& tileKey, const void* data, uint32_t sizeBytes);
        bool isHeightFieldEnabled() const { return heightFieldEnabled_; }

        vk::Buffer getVertexBuffer() const { return vertexBuffer_; }
        vk::Buffer getIndexBuffer() const { return indexBuffer_; }
        vk::Buffer getMeshletBuffer() const { return meshletBuffer_; }
        vk::Buffer getMeshletVertexBuffer() const { return meshletVertexBuffer_; }
        vk::Buffer getMeshletPrimitiveBuffer() const { return meshletPrimitiveBuffer_; }
        vk::Buffer getWeightMapBuffer() const { return weightMapBuffer_; }
        vk::Buffer getHeightFieldBuffer() const { return heightFieldBuffer_; } // VK-1620, null when off

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

        // Release the per-tile arena regions (weight map + VK-1620 heightfield) that are NOT owned
        // by any LOD. Factored out because there are three places a tile entry is destroyed and
        // VK-1613 was exactly the bug of one of them forgetting: freeTileLOD erased the entry while
        // the weight region was still marked allocated, leaking a tile's worth of the arena on every
        // streaming evict/re-add cycle. One function means a future arena cannot repeat it.
        void freeTileArenas(TerrainTileGeometry& tile);
    };
}
