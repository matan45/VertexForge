#pragma once

#include "MeshTypes.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace core
{
    class Device;
    class TransferManager;
}

namespace resource
{
    struct MeshesData;
    struct LODLevel;
}

namespace render::mesh
{

    class MeshGPUCache
    {
    private:
        core::Device& device;
        std::unique_ptr<core::TransferManager> transferManager;
        std::unordered_map<std::string, MeshGPUData> loadedMeshes;

    public:
        explicit MeshGPUCache(core::Device& device);
        ~MeshGPUCache();

        // Non-copyable
        explicit MeshGPUCache(const MeshGPUCache&) = delete;
        MeshGPUCache& operator=(const MeshGPUCache&) = delete;

        std::string loadMesh(std::string_view meshPath);

        std::string uploadMesh(const std::string& meshId, const resource::MeshesData& meshData);

        void unloadMesh(const std::string& meshId);
        void unloadAllMeshes();

        const MeshGPUData* getMesh(const std::string& meshId) const;
        bool isMeshLoaded(const std::string& meshId) const;
        const math::AABB* getMeshBoundingBox(const std::string& meshId) const;
        std::vector<std::string> getLoadedMeshIds() const;

    private:
        void uploadLODLevel(LODGPUBuffers& lodBuffers, const resource::LODLevel& lodLevel);
        void destroyLODBuffers(LODGPUBuffers& lodBuffers);

        // Process mesh data into GPU buffers (shared by loadMesh and uploadMesh)
        bool processMeshData(const resource::MeshesData& meshesData,
                            const std::string& meshId,
                            MeshGPUData& outGpuData);
    };
}
