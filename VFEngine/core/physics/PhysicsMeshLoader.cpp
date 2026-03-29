#include "PhysicsMeshLoader.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/Log.hpp"

namespace core::physics
{
    std::optional<PhysicsMeshData> PhysicsMeshLoader::loadFromFile(
        std::string_view meshPath,
        uint32_t lodLevel,
        uint32_t submeshIndex)
    {
        if (meshPath.empty())
        {
            vfLogWarning("PhysicsMeshLoader: Empty mesh path provided");
            return std::nullopt;
        }

        auto streamHandle = resource::MeshStreamResource::openStream(meshPath);
        if (!streamHandle)
        {
            vfLogWarning("PhysicsMeshLoader: Failed to open mesh file: {}", meshPath);
            return std::nullopt;
        }

        const auto& header = streamHandle->getHeader();
        if (submeshIndex >= header.numSubmeshes)
        {
            vfLogWarning("PhysicsMeshLoader: Submesh index {} out of range (file has {} submeshes)",
                          submeshIndex, header.numSubmeshes);
            return std::nullopt;
        }

        lodLevel = std::min(lodLevel, 3u);

        std::vector<resource::Vertex> vertices;
        std::vector<uint32_t> indices;

        if (!streamHandle->readLODLevel(submeshIndex, lodLevel, vertices, indices))
        {
            vfLogWarning("PhysicsMeshLoader: Failed to read LOD {} from mesh: {}",
                          lodLevel, meshPath);
            return std::nullopt;
        }

        if (vertices.empty())
        {
            vfLogWarning("PhysicsMeshLoader: Mesh has no vertices: {}", meshPath);
            return std::nullopt;
        }

        PhysicsMeshData result;
        result.vertices.reserve(vertices.size());
        for (const auto& vertex : vertices)
        {
            result.vertices.push_back(vertex.position);
        }
        result.indices = std::move(indices);

        vfLogDebug("PhysicsMeshLoader: Loaded {} vertices, {} indices from {} (LOD{})",
                   result.vertices.size(), result.indices.size(), meshPath, lodLevel);

        return result;
    }

    std::optional<PhysicsMeshData> PhysicsMeshLoader::loadAllSubmeshes(
        std::string_view meshPath,
        uint32_t lodLevel)
    {
        if (meshPath.empty())
        {
            vfLogWarning("PhysicsMeshLoader: Empty mesh path provided");
            return std::nullopt;
        }

        auto streamHandle = resource::MeshStreamResource::openStream(meshPath);
        if (!streamHandle)
        {
            vfLogWarning("PhysicsMeshLoader: Failed to open mesh file: {}", meshPath);
            return std::nullopt;
        }

        const auto& header = streamHandle->getHeader();
        if (header.numSubmeshes == 0)
        {
            vfLogWarning("PhysicsMeshLoader: Mesh has no submeshes: {}", meshPath);
            return std::nullopt;
        }

        lodLevel = std::min(lodLevel, 3u);

        PhysicsMeshData result;
        uint32_t totalVertexOffset = 0;

        for (uint32_t i = 0; i < header.numSubmeshes; ++i)
        {
            std::vector<resource::Vertex> vertices;
            std::vector<uint32_t> indices;

            if (!streamHandle->readLODLevel(i, lodLevel, vertices, indices))
            {
                vfLogWarning("PhysicsMeshLoader: Failed to read submesh {} LOD {} from: {}",
                              i, lodLevel, meshPath);
                continue;
            }

            for (const auto& vertex : vertices)
            {
                result.vertices.push_back(vertex.position);
            }

            for (uint32_t idx : indices)
            {
                result.indices.push_back(idx + totalVertexOffset);
            }

            totalVertexOffset += static_cast<uint32_t>(vertices.size());
        }

        if (result.vertices.empty())
        {
            vfLogWarning("PhysicsMeshLoader: No vertex data loaded from: {}", meshPath);
            return std::nullopt;
        }

        vfLogDebug("PhysicsMeshLoader: Loaded {} total vertices, {} indices from {} submeshes (LOD{})",
                   result.vertices.size(), result.indices.size(), header.numSubmeshes, lodLevel);

        return result;
    }

    std::optional<resource::ConvexDecompositionData> PhysicsMeshLoader::loadConvexDecomposition(
        std::string_view meshPath,
        uint32_t submeshIndex)
    {
        if (meshPath.empty())
        {
            vfLogWarning("PhysicsMeshLoader: Empty mesh path provided");
            return std::nullopt;
        }

        auto streamHandle = resource::MeshStreamResource::openStream(meshPath);
        if (!streamHandle)
        {
            vfLogWarning("PhysicsMeshLoader: Failed to open mesh file: {}", meshPath);
            return std::nullopt;
        }

        if (!streamHandle->hasConvexData())
        {
            // File doesn't have convex data - not an error
            return std::nullopt;
        }

        const auto& header = streamHandle->getHeader();
        if (submeshIndex >= header.numSubmeshes)
        {
            vfLogWarning("PhysicsMeshLoader: Submesh index {} out of range (file has {} submeshes)",
                          submeshIndex, header.numSubmeshes);
            return std::nullopt;
        }

        resource::ConvexDecompositionData result;
        if (!streamHandle->readConvexDecomposition(submeshIndex, result))
        {
            vfLogWarning("PhysicsMeshLoader: Failed to read convex decomposition from: {}", meshPath);
            return std::nullopt;
        }

        if (!result.isValid())
        {
            // No convex data for this submesh - not an error
            return std::nullopt;
        }

        vfLogDebug("PhysicsMeshLoader: Loaded convex decomposition with {} hulls from {}",
                   result.hulls.size(), meshPath);

        return result;
    }
}
