#pragma once

#include "GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace core {
    class Device;
    class TransferManager;
}

namespace resource {
    struct MeshStreamHeader;
    struct Vertex;
}

namespace render::mesh {
    class MeshMetadataCache;
    struct MeshMetadata;
    struct MeshRenderData;
}

namespace render::gpudriven {

    // Texture slots for bindless texture array
    enum class TextureSlotType : uint8_t {
        Albedo = 0,
        Normal = 1,
        ORM = 2,
        Metallic = 3,
        Roughness = 4,
        AO = 5,
        Emission = 6,
        Height = 7
    };

    // Callback to resolve material path + texture slot to bindless texture index
    // Returns INVALID_TEXTURE_INDEX if texture is not registered
    using TextureIndexResolver = std::function<uint32_t(const std::string& materialPath, TextureSlotType slot)>;

    class MergedMeshBuffer {
    public:
        explicit MergedMeshBuffer(core::Device& device);
        ~MergedMeshBuffer();

        // Non-copyable
        MergedMeshBuffer(const MergedMeshBuffer&) = delete;
        MergedMeshBuffer& operator=(const MergedMeshBuffer&) = delete;

        // Initialize with maximum expected capacity
        // Default: 15M vertices (~480MB), 45M indices (~180MB) = ~660MB total
        void init(uint32_t maxVertices = 15000000, uint32_t maxIndices = 45000000);

        // Cleanup GPU resources
        void cleanup();

        // Unregister a mesh (marks its space as free, doesn't defrag)
        void unregisterMesh(const std::string& meshPath);

        // Update object buffer from render data list (streaming path)
        // All meshes must be registered via reserveMesh() or registerMeshFromMetadata()
        // The textureResolver callback is used to convert texture paths to bindless indices
        // The time parameter is used to evaluate Time nodes in material shader graphs
        void updateObjects(const std::vector<mesh::MeshRenderData>& renderData,
                          const TextureIndexResolver& textureResolver = nullptr,
                          float time = 0.0f);

        // Register mesh from metadata (reserves space, streaming will upload geometry)
        // Returns the mesh info with submesh locations (LOD states = NotRequested)
        MergedMeshInfo* registerMeshFromMetadata(const std::string& meshPath,
                                                   const mesh::MeshMetadata& metadata);

        // Upload object buffer changes to GPU
        void uploadObjects(vk::CommandBuffer cmd);

        // Accessors
        vk::Buffer getVertexBuffer() const { return vertexBuffer; }
        vk::Buffer getIndexBuffer() const { return indexBuffer; }
        vk::Buffer getObjectBuffer() const { return objectBuffer; }

        uint32_t getTotalVertexCount() const { return totalVertexCount; }
        uint32_t getTotalIndexCount() const { return totalIndexCount; }
        uint32_t getObjectCount() const { return currentObjectCount; }
        uint32_t getMaxObjects() const { return maxObjectCount; }

        // Get submesh location for a specific mesh/submesh (requires index for unique lookup)
        const SubmeshLocation* getSubmeshLocation(const std::string& meshPath,
                                                   const std::string& submeshName,
                                                   uint32_t submeshIndex) const;

        // Get all registered meshes
        const std::vector<MergedMeshInfo>& getRegisteredMeshes() const { return registeredMeshes; }

        // Check if buffer needs rebuild
        bool isDirty() const { return dirty; }
        void markDirty() { dirty = true; }

        // Statistics
        size_t getVertexBufferSize() const { return totalVertexCount * vertexStride; }
        size_t getIndexBufferSize() const { return totalIndexCount * sizeof(uint32_t); }
        size_t getObjectBufferSize() const { return maxObjectCount * sizeof(GPUObjectData); }

        // ===== STREAMING SUPPORT =====

        // Reserve space for a mesh based on stream header info (no geometry yet)
        // Allocates buffer space for all LODs but doesn't upload data
        // Returns the mesh info with submesh locations (LOD states = NotRequested)
        MergedMeshInfo* reserveMesh(const std::string& meshPath,
                                     const resource::MeshStreamHeader& header);

        // Upload a single LOD level for a submesh
        // Called as streaming data arrives
        // vertexData must be array of resource::Vertex (32 bytes each)
        bool uploadLOD(const std::string& meshPath,
                       const std::string& submeshName,
                       uint32_t submeshIndex,
                       uint32_t lodLevel,
                       const resource::Vertex* vertexData, uint32_t vertexCount,
                       const uint32_t* indexData, uint32_t indexCount);

        // Mark a LOD as ready for rendering
        void markLODReady(const std::string& meshPath,
                          const std::string& submeshName,
                          uint32_t submeshIndex,
                          uint32_t lodLevel);

        // Check if mesh has any renderable LOD data
        bool hasRenderableData(const std::string& meshPath) const;

        // Get mutable submesh location (for streaming state updates)
        SubmeshLocation* getSubmeshLocationMutable(const std::string& meshPath,
                                                    const std::string& submeshName,
                                                    uint32_t submeshIndex);

        // Get streaming statistics
        struct StreamingStats {
            uint32_t totalMeshes = 0;
            uint32_t meshesWithRenderableData = 0;
            uint32_t totalLODsReady = 0;
            uint32_t totalLODsPending = 0;
            size_t usedVertexBytes = 0;
            size_t usedIndexBytes = 0;
            size_t reservedVertexBytes = 0;
            size_t reservedIndexBytes = 0;
        };
        StreamingStats getStreamingStats() const;

    private:
        core::Device& device;
        std::unique_ptr<core::TransferManager> transferManager;

        // Merged GPU buffers
        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexBufferMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexBufferMemory;

        // GPU object buffer (for compute shader input)
        vk::Buffer objectBuffer;
        vk::DeviceMemory objectBufferMemory;

        // Staging buffer for object updates
        vk::Buffer objectStagingBuffer;
        vk::DeviceMemory objectStagingMemory;
        void* objectStagingMapped = nullptr;

        // CPU-side object data
        std::vector<GPUObjectData> cpuObjectData;

        // Buffer capacities
        uint32_t maxVertexCount = 0;
        uint32_t maxIndexCount = 0;
        uint32_t maxObjectCount = MAX_GPU_OBJECTS;

        // Current usage
        uint32_t totalVertexCount = 0;
        uint32_t totalIndexCount = 0;
        uint32_t currentObjectCount = 0;

        // Vertex stride (should match resource::Vertex)
        static constexpr uint32_t vertexStride = 32; // vec3 + vec3 + vec2

        // Registered mesh tracking
        std::vector<MergedMeshInfo> registeredMeshes;
        std::unordered_map<std::string, size_t> meshPathToIndex;

        // All submesh locations (flat list for fast lookup)
        std::vector<SubmeshLocation> allSubmeshLocations;
        std::unordered_map<std::string, size_t> submeshKeyToIndex; // "meshPath:submeshName" -> index

        bool initialized = false;
        bool dirty = true;

        // ===== FREE-LIST ALLOCATOR FOR STREAMING =====
        struct FreeBlock {
            uint32_t offset;
            uint32_t size;
        };
        std::vector<FreeBlock> vertexFreeList;
        std::vector<FreeBlock> indexFreeList;

        // Track reserved (allocated but not yet uploaded) space
        uint32_t reservedVertexCount = 0;
        uint32_t reservedIndexCount = 0;

        // Allocate contiguous space from buffer, returns offset or UINT32_MAX on failure
        uint32_t allocateVertexSpace(uint32_t count);
        uint32_t allocateIndexSpace(uint32_t count);

        // Free previously allocated space (adds to free list)
        void freeVertexSpace(uint32_t offset, uint32_t count);
        void freeIndexSpace(uint32_t offset, uint32_t count);

        // Merge adjacent free blocks
        void defragmentFreeList(std::vector<FreeBlock>& freeList);

        // Helper functions
        void createBuffers();
        void destroyBuffers();
        void resizeObjectBuffer(uint32_t newMaxObjects);

        // Append geometry to merged buffers
        void appendVertexData(const void* data, uint32_t vertexCount);
        void appendIndexData(const uint32_t* data, uint32_t indexCount, int32_t vertexOffset);

        // Upload data to specific offset in buffer (for streaming)
        void uploadVertexDataAt(uint32_t offset, const void* data, uint32_t vertexCount);
        void uploadIndexDataAt(uint32_t offset, const uint32_t* data, uint32_t indexCount);

        // Build submesh location key (includes index for uniqueness when names are duplicated)
        static std::string makeSubmeshKey(const std::string& meshPath, const std::string& submeshName, uint32_t submeshIndex) {
            return meshPath + ":" + submeshName + "#" + std::to_string(submeshIndex);
        }

        // Legacy key format (for backward compatibility with non-streaming path)
        static std::string makeSubmeshKeyByName(const std::string& meshPath, const std::string& submeshName) {
            return meshPath + ":" + submeshName;
        }
    };

}
