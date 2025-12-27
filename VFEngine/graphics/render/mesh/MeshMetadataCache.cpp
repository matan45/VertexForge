#include "MeshMetadataCache.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "resource/MeshResource.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Logger.hpp"

namespace render::mesh
{
    std::string MeshMetadataCache::loadMeshMetadata(std::string_view meshPath)
    {
        std::string pathStr(meshPath);

        // Check if already loaded
        if (loadedMeshes.contains(pathStr))
        {
            return pathStr;
        }

        // Try streaming path first (just parses header)
        auto streamHandle = resource::MeshStreamResource::openStream(meshPath);
        if (streamHandle && streamHandle->isOpen())
        {
            return loadFromStreamHeader(pathStr, streamHandle->getHeader());
        }

        // Fallback to full load if streaming isn't supported
        auto meshFuture = resource::ResourceManager::loadMeshAsync(meshPath);
        auto meshesDataPtr = meshFuture.get();

        if (!meshesDataPtr || meshesDataPtr->meshes.empty())
        {
            loggerError("MeshMetadataCache: Failed to load mesh metadata from: {}", meshPath);
            return "";
        }

        return loadFromMeshData(pathStr, *meshesDataPtr);
    }

    std::string MeshMetadataCache::loadFromStreamHeader(const std::string& meshPath,
                                                         const resource::MeshStreamHeader& header)
    {
        // Check if already loaded
        if (loadedMeshes.contains(meshPath))
        {
            return meshPath;
        }

        MeshMetadata metadata{};
        bool boundingBoxInit = false;

        for (uint32_t subIdx = 0; subIdx < header.numSubmeshes; ++subIdx)
        {
            const auto& submeshInfo = header.submeshes[subIdx];

            SubMeshMetadata subMeta{};
            subMeta.name = submeshInfo.name;

            // Copy LOD counts
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                subMeta.lodLevels[lod].vertexCount = submeshInfo.lods[lod].vertexCount;
                subMeta.lodLevels[lod].indexCount = submeshInfo.lods[lod].indexCount;
            }

            // Bounding box will be computed when geometry is first uploaded
            // Initialize to zero (will be updated by MergedMeshBuffer::uploadLOD)
            subMeta.boundingBox.min = glm::vec3(0.0f);
            subMeta.boundingBox.max = glm::vec3(0.0f);

            metadata.subMeshes.push_back(std::move(subMeta));
        }

        loadedMeshes[meshPath] = std::move(metadata);

        loggerInfo("MeshMetadataCache: Loaded metadata for {} ({} submeshes, header-only)",
                   meshPath, header.numSubmeshes);

        return meshPath;
    }

    std::string MeshMetadataCache::loadFromMeshData(const std::string& meshPath,
                                                     const resource::MeshesData& meshData)
    {
        // Check if already loaded
        if (loadedMeshes.contains(meshPath))
        {
            return meshPath;
        }

        MeshMetadata metadata{};
        bool boundingBoxInit = false;

        for (const auto& mesh : meshData.meshes)
        {
            if (mesh.lodLevels.empty() || mesh.lodLevels[0].vertices.empty())
            {
                continue;
            }

            SubMeshMetadata subMeta{};
            subMeta.name = mesh.name;

            // Compute bounding box from LOD0 vertices
            bool subBBInit = false;
            for (const auto& vertex : mesh.lodLevels[0].vertices)
            {
                if (!subBBInit)
                {
                    subMeta.boundingBox.min = vertex.position;
                    subMeta.boundingBox.max = vertex.position;
                    subBBInit = true;
                }
                else
                {
                    subMeta.boundingBox.expand(vertex.position);
                }

                if (!boundingBoxInit)
                {
                    metadata.boundingBox.min = vertex.position;
                    metadata.boundingBox.max = vertex.position;
                    boundingBoxInit = true;
                }
                else
                {
                    metadata.boundingBox.expand(vertex.position);
                }
            }

            // Copy LOD counts
            uint32_t lodCount = static_cast<uint32_t>(
                std::min(mesh.lodLevels.size(), static_cast<size_t>(resource::LOD_LEVEL_COUNT)));

            for (uint32_t lod = 0; lod < lodCount; ++lod)
            {
                subMeta.lodLevels[lod].vertexCount = static_cast<uint32_t>(mesh.lodLevels[lod].vertices.size());
                subMeta.lodLevels[lod].indexCount = static_cast<uint32_t>(mesh.lodLevels[lod].indices.size());
            }

            // Fill remaining LODs with last available
            for (uint32_t lod = lodCount; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                subMeta.lodLevels[lod] = subMeta.lodLevels[lodCount - 1];
            }

            metadata.subMeshes.push_back(std::move(subMeta));
        }

        if (metadata.subMeshes.empty())
        {
            loggerError("MeshMetadataCache: Mesh has no valid submeshes: {}", meshPath);
            return "";
        }

        loadedMeshes[meshPath] = std::move(metadata);

        loggerInfo("MeshMetadataCache: Loaded metadata for {} ({} submeshes, full parse)",
                   meshPath, loadedMeshes[meshPath].subMeshes.size());

        return meshPath;
    }

    void MeshMetadataCache::unloadMesh(const std::string& meshId)
    {
        auto it = loadedMeshes.find(meshId);
        if (it != loadedMeshes.end())
        {
            loadedMeshes.erase(it);
            loggerInfo("MeshMetadataCache: Unloaded metadata for {}", meshId);
        }
    }

    void MeshMetadataCache::unloadAllMeshes()
    {
        loadedMeshes.clear();
        loggerInfo("MeshMetadataCache: Unloaded all mesh metadata");
    }

    const MeshMetadata* MeshMetadataCache::getMesh(const std::string& meshId) const
    {
        auto it = loadedMeshes.find(meshId);
        return (it != loadedMeshes.end()) ? &it->second : nullptr;
    }

    bool MeshMetadataCache::isMeshLoaded(const std::string& meshId) const
    {
        return loadedMeshes.contains(meshId);
    }

    const math::AABB* MeshMetadataCache::getMeshBoundingBox(const std::string& meshId) const
    {
        auto it = loadedMeshes.find(meshId);
        return (it != loadedMeshes.end()) ? &it->second.boundingBox : nullptr;
    }

    std::vector<std::string> MeshMetadataCache::getLoadedMeshIds() const
    {
        std::vector<std::string> ids;
        ids.reserve(loadedMeshes.size());
        for (const auto& [path, _] : loadedMeshes)
        {
            ids.push_back(path);
        }
        return ids;
    }

    const SubMeshMetadata* MeshMetadataCache::getSubmesh(const std::string& meshId,
                                                          const std::string& submeshName) const
    {
        const auto* mesh = getMesh(meshId);
        if (!mesh) return nullptr;

        for (const auto& sub : mesh->subMeshes)
        {
            if (sub.name == submeshName) return &sub;
        }
        return nullptr;
    }

    const SubMeshMetadata* MeshMetadataCache::getSubmesh(const std::string& meshId,
                                                          uint32_t submeshIndex) const
    {
        const auto* mesh = getMesh(meshId);
        if (!mesh || submeshIndex >= mesh->subMeshes.size()) return nullptr;
        return &mesh->subMeshes[submeshIndex];
    }
}
