#pragma once

#include "GPUDrivenTypes.hpp"
#include "FreeListAllocator.hpp"
#include <vulkan/vulkan.hpp>
#include <functional>
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
    struct MeshStreamHeader;
    struct Vertex;
}

namespace render::mesh
{
    struct MeshMetadata;
    struct MeshRenderData;
}

namespace render::gpudriven
{
   
    enum class TextureSlotType : uint8_t
    {
        Albedo = 0,
        Normal = 1,
        ORM = 2,
        Metallic = 3,
        Roughness = 4,
        AO = 5,
        Emission = 6,
        Height = 7
    };

    using TextureIndexResolver = std::function<uint32_t(const std::string& materialPath, TextureSlotType slot)>;
    
    using ShaderGroupResolver = std::function<uint32_t(const std::string& materialPath)>;

    class MergedMeshBuffer
    {
    private:
        core::Device& device;
        std::unique_ptr<core::TransferManager> transferManager;

        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexBufferMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexBufferMemory;

        vk::Buffer objectBuffer;
        vk::DeviceMemory objectBufferMemory;

        vk::Buffer objectStagingBuffer;
        vk::DeviceMemory objectStagingMemory;
        void* objectStagingMapped = nullptr;

        std::vector<GPUObjectData> cpuObjectData;

        uint32_t maxVertexCount = 0;
        uint32_t maxIndexCount = 0;
        uint32_t maxObjectCount = MAX_GPU_OBJECTS;

        uint32_t totalVertexCount = 0;
        uint32_t totalIndexCount = 0;
        uint32_t currentObjectCount = 0;

        static constexpr uint32_t vertexStride = 32; // vec3 + vec3 + vec2

        std::vector<MergedMeshInfo> registeredMeshes;
        std::unordered_map<std::string, size_t> meshPathToIndex;

        std::vector<SubmeshLocation> allSubmeshLocations;
        std::unordered_map<std::string, size_t> submeshKeyToIndex; // "meshPath:submeshName" -> index

        bool initialized = false;

        FreeListAllocator vertexAllocator;
        FreeListAllocator indexAllocator;

    public:
        explicit MergedMeshBuffer(core::Device& device);
        ~MergedMeshBuffer();

        MergedMeshBuffer(const MergedMeshBuffer&) = delete;
        MergedMeshBuffer& operator=(const MergedMeshBuffer&) = delete;

        void init(uint32_t maxVertices = 15000000, uint32_t maxIndices = 45000000);

        void cleanup();

        void updateObjects(const std::vector<mesh::MeshRenderData>& renderData,
                           const TextureIndexResolver& textureResolver = nullptr,
                           const ShaderGroupResolver& shaderGroupResolver = nullptr,
                           float time = 0.0f);

        MergedMeshInfo* registerMeshFromMetadata(const std::string& meshPath,
                                                 const mesh::MeshMetadata& metadata);

        void uploadObjects(vk::CommandBuffer cmd);

        // Accessors
        vk::Buffer getVertexBuffer() const { return vertexBuffer; }
        vk::Buffer getObjectBuffer() const { return objectBuffer; }

        uint32_t getTotalVertexCount() const { return totalVertexCount; }
        uint32_t getTotalIndexCount() const { return totalIndexCount; }
        uint32_t getObjectCount() const { return currentObjectCount; }

        const SubmeshLocation* getSubmeshLocation(const std::string& meshPath,
                                                  const std::string& submeshName,
                                                  uint32_t submeshIndex) const;

        const std::vector<MergedMeshInfo>& getRegisteredMeshes() const { return registeredMeshes; }

        // ===== STREAMING SUPPORT =====

        MergedMeshInfo* reserveMesh(const std::string& meshPath,
                                    const resource::MeshStreamHeader& header);

        bool uploadLOD(const std::string& meshPath,
                       const std::string& submeshName,
                       uint32_t submeshIndex,
                       uint32_t lodLevel,
                       const resource::Vertex* vertexData, uint32_t vertexCount,
                       const uint32_t* indexData, uint32_t indexCount);

        void markLODReady(const std::string& meshPath,
                          const std::string& submeshName,
                          uint32_t submeshIndex,
                          uint32_t lodLevel);

        bool hasRenderableData(const std::string& meshPath) const;

        SubmeshLocation* getSubmeshLocationMutable(const std::string& meshPath,
                                                   const std::string& submeshName,
                                                   uint32_t submeshIndex);

        // Ensure all pending async transfers are complete before rendering
        void flushPendingTransfers();

    private:
        void createBuffers();
        void destroyBuffers();

        void uploadVertexDataAt(uint32_t offset, const void* data, uint32_t vertexCount);
        void uploadIndexDataAt(uint32_t offset, const uint32_t* data, uint32_t indexCount);

        bool allocateLODSpace(SubmeshLocation& loc, uint32_t lodLevel,
                              uint32_t vertexCount, uint32_t indexCount,
                              const std::string& meshPath);

        void populateObjectData(GPUObjectData& obj,
                                const mesh::MeshRenderData& meshRender,
                                const SubmeshLocation& submeshLoc,
                                const TextureIndexResolver& textureResolver,
                                const ShaderGroupResolver& shaderGroupResolver,
                                float time);

        static std::string makeSubmeshKey(const std::string& meshPath, const std::string& submeshName,
                                          uint32_t submeshIndex)
        {
            return meshPath + ":" + submeshName + "#" + std::to_string(submeshIndex);
        }
    };
}
