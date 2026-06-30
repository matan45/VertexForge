#include "ConvexDecompositionRegenerator.hpp"

#include "MeshLODGenerator.hpp"
#include "print/Log.hpp"
#include "resource/ConvexDecompositionSidecar.hpp"
#include "resource/MeshStreamHandle.hpp"

#include <filesystem>
#include <vector>

namespace types
{
    namespace
    {
        LODMeshData makeLODMeshData(std::vector<resource::Vertex>&& vertices,
                                    std::vector<uint32_t>&& indices)
        {
            LODMeshData result;
            result.vertices = std::move(vertices);
            result.indices = std::move(indices);
            return result;
        }
    }

    ConvexRegenerationResult ConvexDecompositionRegenerator::regenerate(
        const std::string& meshPath,
        int32_t submeshIndex,
        importConfig::MeshImportConfig config,
        ConvexProgressCallback progressCallback,
        std::atomic<bool>* cancelFlag)
    {
        ConvexRegenerationResult result;
        result.sidecarPath = resource::ConvexDecompositionSidecar::sidecarPathForMesh(meshPath).string();

        if (meshPath.empty())
        {
            result.message = "No mesh path resolved for collider";
            return result;
        }

        if (!std::filesystem::exists(meshPath))
        {
            result.message = "Mesh file does not exist: " + meshPath;
            return result;
        }

        auto stream = resource::MeshStreamResource::openStream(meshPath);
        if (!stream)
        {
            result.message = "Failed to open mesh stream: " + meshPath;
            return result;
        }

        const auto& header = stream->getHeader();
        if (header.numSubmeshes == 0)
        {
            result.message = "Mesh has no submeshes: " + meshPath;
            return result;
        }

        std::vector<uint32_t> targetSubmeshes;
        if (submeshIndex >= 0)
        {
            if (static_cast<uint32_t>(submeshIndex) >= header.numSubmeshes)
            {
                result.message = "Submesh index is out of range";
                return result;
            }
            targetSubmeshes.push_back(static_cast<uint32_t>(submeshIndex));
        }
        else
        {
            targetSubmeshes.reserve(header.numSubmeshes);
            for (uint32_t i = 0; i < header.numSubmeshes; ++i)
                targetSubmeshes.push_back(i);
        }

        config.generateConvexDecomposition = true;

        MeshLODGenerator generator;
        std::vector<resource::ConvexDecompositionSidecarEntry> entries;
        entries.reserve(targetSubmeshes.size());

        for (size_t targetIdx = 0; targetIdx < targetSubmeshes.size(); ++targetIdx)
        {
            if (cancelFlag && cancelFlag->load())
            {
                result.message = "Convex decomposition regeneration cancelled";
                return result;
            }

            const uint32_t currentSubmesh = targetSubmeshes[targetIdx];
            std::vector<resource::Vertex> vertices;
            std::vector<uint32_t> indices;
            if (!stream->readLODLevel(currentSubmesh, 0, vertices, indices))
            {
                result.message = "Failed to read LOD0 geometry from mesh";
                return result;
            }

            if (vertices.empty() || indices.empty())
            {
                result.message = "Selected mesh geometry is empty";
                return result;
            }

            auto meshData = makeLODMeshData(std::move(vertices), std::move(indices));
            const float submeshBase = static_cast<float>(targetIdx) / static_cast<float>(targetSubmeshes.size());
            const float submeshScale = 1.0f / static_cast<float>(targetSubmeshes.size());

            auto data = generator.generateConvexDecomposition(
                meshData,
                config,
                [&](float progress, std::string_view stage)
                {
                    if (progressCallback)
                    {
                        progressCallback(submeshBase + progress * submeshScale, stage);
                    }
                },
                cancelFlag);

            if (!data.isValid())
            {
                result.message = "V-HACD generated no valid hulls";
                return result;
            }

            result.hullCount += static_cast<uint32_t>(data.hulls.size());
            entries.push_back(resource::ConvexDecompositionSidecarEntry{
                currentSubmesh,
                std::move(data)
            });
        }

        if (!resource::ConvexDecompositionSidecar::upsert(meshPath, entries))
        {
            result.message = "Failed to write convex decomposition sidecar";
            return result;
        }

        result.success = true;
        result.submeshCount = static_cast<uint32_t>(entries.size());
        result.message = "Convex decomposition regenerated";
        vfLogInfo("Regenerated convex decomposition for {} submesh(es), {} hull(s): {}",
                  result.submeshCount, result.hullCount, meshPath);
        return result;
    }
}
