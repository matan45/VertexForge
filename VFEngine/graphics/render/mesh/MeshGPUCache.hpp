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
}

namespace render::mesh
{
    class MeshGPUCache
    {
    public:
        explicit MeshGPUCache(core::Device& device);
        ~MeshGPUCache();

        // Non-copyable
        MeshGPUCache(const MeshGPUCache&) = delete;
        MeshGPUCache& operator=(const MeshGPUCache&) = delete;

        // Mesh loading from file
        std::string loadMesh(std::string_view meshPath);

        // Upload procedural mesh data directly (bypasses file loading)
        std::string uploadMesh(const std::string& meshId, const resource::MeshesData& meshData);

        // Mesh unloading
        void unloadMesh(const std::string& meshId);
        void unloadAllMeshes();

        // Mesh queries
        const MeshGPUData* getMesh(const std::string& meshId) const;
        bool isMeshLoaded(const std::string& meshId) const;
        const math::AABB* getMeshBoundingBox(const std::string& meshId) const;
        std::vector<std::string> getLoadedMeshIds() const;

    private:
        core::Device& device;
        std::unique_ptr<core::TransferManager> transferManager;
        std::unordered_map<std::string, MeshGPUData> loadedMeshes;
    };
}
