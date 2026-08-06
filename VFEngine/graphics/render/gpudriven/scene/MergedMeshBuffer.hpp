#pragma once

#include "../GPUDrivenTypes.hpp"
#include "../FreeListAllocator.hpp"
#include "../../material/MaterialPBRExtractor.hpp"
#include "../../../core/RenderManager.hpp"
#include "material/MaterialManager.hpp"
#include "memory/VramAssetSnapshot.hpp"
#include <vulkan/vulkan.hpp>
#include "../../../core/VulkanMemoryManager.hpp"
#include <entt/entt.hpp>
#include <array>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

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

    // VK-1493: resolves {shadingModel, toonProfileIndex} for a material path so the
    // object-streaming path can pack toon flags from defaultMaterialPath, matching
    // the edit-mode pbrCache fallback in MergedMeshBuffer::populateObjectData.
    using ShadingResolver = std::function<std::pair<uint8_t, uint8_t>(const std::string& materialPath)>;

    // VK-1620/VK-1648: the same defaultMaterialPath fallback as ShadingResolver, for mesh-into-
    // terrain blending. Without it a blend authored on the mesh's own material rather than on a
    // per-submesh override applies in the editor viewport (MergedMeshBuffer::populateObjectData
    // reads pbrCache) but not in play mode or an exported game, where the object-streaming path
    // sees only `subMat` — the prop's base dissolves into the ground until you press Play.
    struct TerrainBlendParams
    {
        bool blendToTerrain = false;
        float band = 0.0f;
        float contrast = 0.0f;
    };
    using TerrainBlendResolver = std::function<TerrainBlendParams(const std::string& materialPath)>;

    struct ObjectResolvers
    {
        TextureIndexResolver textureResolver;
        ShaderGroupResolver shaderGroupResolver;
        BoneOffsetResolver boneOffsetResolver;
        ShadingResolver shadingResolver;
        TerrainBlendResolver terrainBlendResolver;
        float time = 0.0f;
        glm::vec3 cameraPosition{0.0f};
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
        core::VulkanAllocation vertexBufferAllocation;
        vk::Buffer indexBuffer;
        core::VulkanAllocation indexBufferAllocation;

        vk::Buffer objectBuffer;
        core::VulkanAllocation objectBufferAllocation;

        vk::Buffer instanceTransformBuffer;
        core::VulkanAllocation instanceTransformBufferAllocation;

        // Per-frame staging buffers to allow CPU/GPU overlap
        struct StagingFrame
        {
            vk::Buffer objectStagingBuffer;
            core::VulkanAllocation objectStagingAllocation;
            void* objectStagingMapped = nullptr;

            vk::Buffer instanceStagingBuffer;
            core::VulkanAllocation instanceStagingAllocation;
            void* instanceStagingMapped = nullptr;

            vk::Buffer activeIndexStagingBuffer;
            core::VulkanAllocation activeIndexStagingAllocation;
            void* activeIndexStagingMapped = nullptr;
        };

        std::array<StagingFrame, core::MAX_FRAMES_IN_FLIGHT> stagingFrames{};
        uint32_t currentStagingFrame = 0;

        std::vector<GPUInstanceTransform> cpuInstanceTransforms;
        uint32_t maxInstanceCount = MAX_GPU_INSTANCES;
        uint32_t currentInstanceCount = 0;

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

        // VK-1539: resident VRAM bytes for one mesh = sum over its submeshes' LODs that are
        // actually resident (LODStreamState::Ready) of vertexCount*vertexStride +
        // indexCount*sizeof(uint32). Co-located with vertexStride, the layout constant it uses.
        uint64_t residentBytes(const MergedMeshInfo& mesh) const;

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
        // Resolved named-parameter overrides per instance path, so Time-driven emission
        // evaluation honors instance overrides (lifecycle mirrors instanceToParentCache)
        std::unordered_map<std::string, mesh::MaterialPBRExtractor::ParameterOverrides> instanceOverrideCache;
        material::CallbackId materialChangeCallbackId{};

        // VK-1490 editor selection outline: entt ids of the selected entities and
        // the GPU object slots they resolved to in the last updateObjects rebuild
        // (edit-mode path only — persistent play-mode slots never record here).
        std::unordered_set<uint32_t> selectedEntities;
        std::vector<uint32_t> selectedObjectSlots;

        void recordSelectedSlot(entt::entity entity, uint32_t slot)
        {
            if (selectedEntities.empty()) return;
            if (selectedEntities.count(static_cast<uint32_t>(entity)) != 0)
            {
                selectedObjectSlots.push_back(slot);
            }
        }

        // Persistent slot mode (play mode streaming)
        FreeListAllocator objectAllocator;
        std::unordered_map<uint64_t, uint32_t> entityToSlot;    // Entity UUID -> GPU slot
        std::vector<uint32_t> activeObjectIndices;
        uint32_t activeObjectCount = 0;
        std::vector<uint32_t> dirtySlots;
        bool persistentMode = false;

        // Active-index GPU buffer
        vk::Buffer activeIndexBuffer;
        core::VulkanAllocation activeIndexBufferAllocation;

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

        // VK-1490: editor selection for the outline mask pass. updateObjects
        // rebuilds selectedObjectSlots from these entt ids each frame; an empty
        // set costs nothing on the hot path.
        void setSelectedEntities(std::unordered_set<uint32_t> entityIds)
        {
            selectedEntities = std::move(entityIds);
            if (selectedEntities.empty()) selectedObjectSlots.clear();
        }
        const std::vector<uint32_t>& getSelectedObjectSlots() const { return selectedObjectSlots; }

        void uploadObjects(vk::CommandBuffer cmd);
        void uploadInstances(vk::CommandBuffer cmd);

        vk::Buffer getVertexBuffer() const { return vertexBuffer; }
        vk::Buffer getIndexBuffer() const { return indexBuffer; }
        vk::Buffer getObjectBuffer() const { return objectBuffer; }
        vk::Buffer getInstanceTransformBuffer() const { return instanceTransformBuffer; }
        const std::vector<GPUObjectData>& getCPUObjectData() const { return cpuObjectData; }
        const std::vector<GPUInstanceTransform>& getCPUInstanceTransforms() const { return cpuInstanceTransforms; }
        uint32_t getInstanceCount() const { return currentInstanceCount; }

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
        const std::vector<SubmeshLocation>& getAllSubmeshLocations() const { return allSubmeshLocations; }

        // VK-1539 memory profiler: append one per-mesh VRAM row {meshPath, residentBytes} for each
        // registered mesh (skipping zero-byte meshes with no resident LOD) and accumulate meshTotal.
        // Walks registeredMeshes, so it MUST be called on the render thread that owns it.
        void appendVramRows(std::vector<memory::VramAssetRow>& out, uint64_t& meshTotal) const;

        // Persistent slot mode (play mode streaming)
        void setPersistentMode(bool enabled);
        bool isPersistentMode() const { return persistentMode; }
        uint32_t allocateObjectSlot();
        void freeObjectSlot(uint32_t slot);
        void updateObjectAtSlot(uint32_t slot, const GPUObjectData& data);
        void rebuildActiveIndexList();
        void uploadDirtyObjects(vk::CommandBuffer cmd);
        void uploadActiveIndices(vk::CommandBuffer cmd);
        uint32_t getActiveObjectCount() const { return activeObjectCount; }
        const std::vector<uint32_t>& getActiveObjectIndices() const { return activeObjectIndices; }
        vk::Buffer getActiveIndexBuffer() const { return activeIndexBuffer; }
        void mapEntityToSlot(uint64_t entityUUID, uint32_t slot) { entityToSlot[entityUUID] = slot; }
        void unmapEntitySlot(uint64_t entityUUID) { entityToSlot.erase(entityUUID); }
        uint32_t getEntitySlotCount() const { return static_cast<uint32_t>(entityToSlot.size()); }

        uint32_t getSlotForEntityUUID(uint64_t uuid) const
        {
            auto it = entityToSlot.find(uuid);
            return it != entityToSlot.end() ? it->second : UINT32_MAX;
        }
        GPUObjectData& getMutableObjectData(uint32_t slot)
        {
            assert(slot < cpuObjectData.size() && "MergedMeshBuffer: slot out of bounds");
            return cpuObjectData[slot];
        }
        void markSlotDirty(uint32_t slot) { dirtySlots.push_back(slot); }

        // Acceleration structure callbacks - invoked when submesh LOD 0 becomes ready or mesh is freed
        std::function<void(const std::string& meshPath, const std::string& submeshName,
                           uint32_t submeshIndex, const SubmeshLocation& loc)> onSubmeshLOD0Ready;
        std::function<void(const std::string& meshPath, const std::string& submeshName,
                           uint32_t submeshIndex)> onSubmeshRemoved;

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

        // Advance to next staging frame (call once per frame before uploads)
        void advanceStagingFrame() { currentStagingFrame = (currentStagingFrame + 1) % core::MAX_FRAMES_IN_FLIGHT; }
        uint32_t getCurrentStagingFrame() const { return currentStagingFrame; }

    private:
        void createBuffers();
        void createGeometryBuffers();
        void createObjectBuffers();
        void createInstanceBuffers();
        void createActiveIndexBuffers();
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

        void updateObjectsSequential(const std::vector<mesh::MeshRenderData>& renderData,
                                     const ObjectResolvers& resolvers);

        struct ObjectWorkItem
        {
            const mesh::MeshRenderData* meshRender;
            const SubmeshLocation* submeshLoc;
        };
        std::vector<ObjectWorkItem> parallelWorkItems;

        static constexpr uint32_t PARALLEL_OBJECT_THRESHOLD = 512;

        static std::string makeSubmeshKey(const std::string& meshPath, const std::string& submeshName,
                                          uint32_t submeshIndex)
        {
            return meshPath + ":" + submeshName + "#" + std::to_string(submeshIndex);
        }
    };
}
