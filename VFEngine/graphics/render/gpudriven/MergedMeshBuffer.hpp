#pragma once

#include "GPUDrivenTypes.hpp"
#include "FreeListAllocator.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "material/MaterialManager.hpp"
#include <vulkan/vulkan.hpp>
#include <entt/entt.hpp>
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

    using BoneOffsetResolver = std::function<uint32_t(entt::entity entity)>;

    using LightmapIndexResolver = std::function<uint32_t(const std::string& lightmapPath)>;

    struct ObjectResolvers
    {
        TextureIndexResolver textureResolver;
        ShaderGroupResolver shaderGroupResolver;
        BoneOffsetResolver boneOffsetResolver;
        LightmapIndexResolver lightmapResolver;
        float time = 0.0f;
    };

    struct LODUploadData
    {
        const resource::Vertex* vertexData = nullptr;
        uint32_t vertexCount = 0;
        const uint32_t* indexData = nullptr;
        uint32_t indexCount = 0;
    };

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
        uint32_t transparentObjectCount = 0;

        uint32_t peakObjectCount = 0;
        uint32_t peakVertexCount = 0;
        uint32_t peakIndexCount = 0;

        static constexpr uint32_t vertexStride = 64;

        std::vector<MergedMeshInfo> registeredMeshes;
        std::vector<size_t> freeMeshSlots;
        std::unordered_map<std::string, size_t> meshPathToIndex;

        std::vector<SubmeshLocation> allSubmeshLocations;
        std::unordered_map<std::string, size_t> submeshKeyToIndex;

        bool initialized = false;

        FreeListAllocator vertexAllocator;
        FreeListAllocator indexAllocator;

        std::unordered_map<std::string, mesh::ExtractedPBRValues> pbrCache;
        std::unordered_map<std::string, std::string> instanceToParentCache;
        material::CallbackId materialChangeCallbackId{};

    public:
        explicit MergedMeshBuffer(core::Device& device);
        ~MergedMeshBuffer();

        MergedMeshBuffer(const MergedMeshBuffer&) = delete;
        MergedMeshBuffer& operator=(const MergedMeshBuffer&) = delete;

        // Note: Large terrains (16x16 Ultra) may need ~100M+ indices
        // Default increased to support larger terrain configurations
        void init(uint32_t maxVertices = 25000000, uint32_t maxIndices = 100000000);

        void cleanup();

        void updateObjects(const std::vector<mesh::MeshRenderData>& renderData,
                           const ObjectResolvers& resolvers = {});

        void uploadObjects(vk::CommandBuffer cmd);

        vk::Buffer getVertexBuffer() const { return vertexBuffer; }
        vk::Buffer getIndexBuffer() const { return indexBuffer; }
        vk::Buffer getObjectBuffer() const { return objectBuffer; }
        const std::vector<GPUObjectData>& getCPUObjectData() const { return cpuObjectData; }

        uint32_t getTotalVertexCount() const { return totalVertexCount; }
        uint32_t getTotalIndexCount() const { return totalIndexCount; }
        uint32_t getObjectCount() const { return currentObjectCount; }
        uint32_t getTransparentObjectCount() const { return transparentObjectCount; }

        uint32_t getPeakObjectCount() const { return peakObjectCount; }
        uint32_t getPeakVertexCount() const { return peakVertexCount; }
        uint32_t getPeakIndexCount() const { return peakIndexCount; }
        uint32_t getMaxVertexCount() const { return maxVertexCount; }
        uint32_t getMaxIndexCount() const { return maxIndexCount; }
        uint32_t getMaxObjectCount() const { return maxObjectCount; }
        uint32_t getRegisteredMeshCount() const { return static_cast<uint32_t>(meshPathToIndex.size()); }
        uint32_t getRegisteredSubmeshCount() const { return static_cast<uint32_t>(submeshKeyToIndex.size()); }
        float getVertexUtilization() const { return maxVertexCount > 0 ? static_cast<float>(totalVertexCount) / maxVertexCount : 0.0f; }
        float getIndexUtilization() const { return maxIndexCount > 0 ? static_cast<float>(totalIndexCount) / maxIndexCount : 0.0f; }

        const SubmeshLocation* getSubmeshLocation(const std::string& meshPath,
                                                  const std::string& submeshName,
                                                  uint32_t submeshIndex) const;

        const std::vector<MergedMeshInfo>& getRegisteredMeshes() const { return registeredMeshes; }

        MergedMeshInfo* reserveMesh(const std::string& meshPath,
                                    const resource::MeshStreamHeader& header);

        void freeMesh(const std::string& meshPath);

        bool uploadLOD(const std::string& meshPath,
                       const std::string& submeshName,
                       uint32_t submeshIndex,
                       uint32_t lodLevel,
                       const LODUploadData& data);

        void markLODReady(const std::string& meshPath,
                          const std::string& submeshName,
                          uint32_t submeshIndex,
                          uint32_t lodLevel);

        SubmeshLocation* getSubmeshLocationMutable(const std::string& meshPath,
                                                   const std::string& submeshName,
                                                   uint32_t submeshIndex);

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
                                const ObjectResolvers& resolvers);

        void populateLODData(GPUObjectData& obj, const mesh::MeshRenderData& meshRender,
                             const SubmeshLocation& submeshLoc, const BoneOffsetResolver& boneOffsetResolver);

        std::string resolveMaterialProperties(GPUObjectData& obj, const mesh::MeshRenderData& meshRender,
                                              const SubmeshLocation& submeshLoc);

        void applyDynamicEmission(GPUObjectData& obj, const std::string& materialPath, float time);

        static std::string makeSubmeshKey(const std::string& meshPath, const std::string& submeshName,
                                          uint32_t submeshIndex)
        {
            return meshPath + ":" + submeshName + "#" + std::to_string(submeshIndex);
        }
    };
}
