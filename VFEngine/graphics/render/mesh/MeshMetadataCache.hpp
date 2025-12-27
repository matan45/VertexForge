#pragma once

#include "MeshTypes.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace resource
{
    struct MeshesData;
    struct MeshStreamHeader;
}

namespace render::mesh
{
    /**
     * MeshMetadataCache - Lightweight mesh metadata storage
     *
     * Unlike MeshGPUCache, this class stores ONLY metadata (submesh names,
     * bounding boxes, LOD vertex/index counts). No GPU buffers are created.
     *
     * The GPU-driven rendering path uses MeshStreamManager to stream geometry
     * directly into MergedMeshBuffer. This cache provides the metadata needed
     * for scene management, culling bounds, and submesh information.
     *
     * For CPU fallback rendering (preview, debug), use MeshGPUCache instead.
     */
    class MeshMetadataCache
    {
    private:
        std::unordered_map<std::string, MeshMetadata> loadedMeshes;

    public:
        MeshMetadataCache() = default;
        ~MeshMetadataCache() = default;

        // Non-copyable
        MeshMetadataCache(const MeshMetadataCache&) = delete;
        MeshMetadataCache& operator=(const MeshMetadataCache&) = delete;

        // Load mesh metadata from file (parses header only, no GPU upload)
        std::string loadMeshMetadata(std::string_view meshPath);

        // Load mesh metadata from pre-parsed stream header
        std::string loadFromStreamHeader(const std::string& meshPath,
                                          const resource::MeshStreamHeader& header);

        // Load mesh metadata from fully loaded mesh data
        std::string loadFromMeshData(const std::string& meshPath,
                                      const resource::MeshesData& meshData);

        // Unload mesh metadata
        void unloadMesh(const std::string& meshId);
        void unloadAllMeshes();

        // Query mesh metadata
        const MeshMetadata* getMesh(const std::string& meshId) const;
        bool isMeshLoaded(const std::string& meshId) const;
        const math::AABB* getMeshBoundingBox(const std::string& meshId) const;
        std::vector<std::string> getLoadedMeshIds() const;

        // Get submesh info
        const SubMeshMetadata* getSubmesh(const std::string& meshId,
                                           const std::string& submeshName) const;
        const SubMeshMetadata* getSubmesh(const std::string& meshId,
                                           uint32_t submeshIndex) const;
    };
}
