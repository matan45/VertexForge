#include "print/Log.hpp"
#include "ProceduralMeshWriter.hpp"
#include "MeshFileLayout.hpp"
#include "MeshLODGenerator.hpp"
#include "MeshSerializer.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace
{
    // Mirrors MeshStreamHandle's read-side caps (MeshStreamHandle.hpp:62-64), which are private.
    // Validating here turns "the file writes fine and then silently refuses to load" into an error
    // at the point the caller can still do something about it.
    constexpr size_t kMaxVertexCount = 10'000'000;
    constexpr size_t kMaxIndexCount = 30'000'000;
    constexpr size_t kMaxSubmeshCount = 10'000;

    [[nodiscard]] std::string describe(const types::ProceduralSubmesh& submesh, uint32_t lod)
    {
        return "submesh '" + submesh.name + "' LOD " + std::to_string(lod);
    }
}

namespace types
{
    ProceduralMeshWriteResult ProceduralMeshWriter::write(const std::string& outputPath,
                                                          const std::vector<ProceduralSubmesh>& submeshes)
    {
        ProceduralMeshWriteResult result;
        result.outputPath = outputPath;

        if (submeshes.empty())
        {
            result.message = "no submeshes to write";
            vfLogError("ProceduralMeshWriter: {} ({})", result.message, outputPath);
            return result;
        }

        if (submeshes.size() > kMaxSubmeshCount)
        {
            result.message = "submesh count " + std::to_string(submeshes.size())
                           + " exceeds the reader limit of " + std::to_string(kMaxSubmeshCount);
            vfLogError("ProceduralMeshWriter: {} ({})", result.message, outputPath);
            return result;
        }

        // Validate everything BEFORE opening the stream: a caller that hands over a half-built LOD
        // chain should get told, not get a file the streamer rejects at load time.
        for (const ProceduralSubmesh& submesh : submeshes)
        {
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                const LODMeshData& level = submesh.lods[lod];
                if (level.vertices.empty() || level.indices.empty())
                    result.message = describe(submesh, lod) + " is empty (all "
                                   + std::to_string(resource::LOD_LEVEL_COUNT) + " levels are required)";
                else if (level.indices.size() % 3 != 0)
                    result.message = describe(submesh, lod) + " has "
                                   + std::to_string(level.indices.size()) + " indices, not a multiple of 3";
                else if (level.vertices.size() > kMaxVertexCount)
                    result.message = describe(submesh, lod) + " exceeds the reader's vertex limit";
                else if (level.indices.size() > kMaxIndexCount)
                    result.message = describe(submesh, lod) + " exceeds the reader's index limit";

                if (!result.message.empty())
                {
                    vfLogError("ProceduralMeshWriter: {} ({})", result.message, outputPath);
                    return result;
                }
            }
            result.totalVertices += submesh.lods[0].vertices.size();
        }

        std::error_code ec;
        const std::filesystem::path finalPath(outputPath);
        if (finalPath.has_parent_path())
        {
            std::filesystem::create_directories(finalPath.parent_path(), ec);
            if (ec)
            {
                result.message = "failed to create output directory: " + ec.message();
                vfLogError("ProceduralMeshWriter: {} ({})", result.message, outputPath);
                return result;
            }
        }

        // Write beside the target and rename, so a failure part-way through cannot leave a
        // truncated file where a valid asset used to be.
        const std::filesystem::path tempPath = finalPath.string() + ".tmp";
        {
            std::ofstream outFile(tempPath, std::ios::binary);
            if (!outFile)
            {
                result.message = "failed to open for writing";
                vfLogError("ProceduralMeshWriter: {} ({})", result.message, tempPath.string());
                return result;
            }

            meshlayout::writeFileHeader(outFile, static_cast<uint32_t>(submeshes.size()));

            MeshLODGenerator lodGen;
            MeshSerializer serializer;

            for (const ProceduralSubmesh& submesh : submeshes)
            {
                meshlayout::writeSubmeshHeader(outFile, submesh.name,
                                               submesh.lods[0].vertices.size(),
                                               submesh.lods[0].indices.size() / 3);

                for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                    serializer.writeLODLevelCompressed(outFile, submesh.lods[lod]);

                std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT> meshletResults;
                for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                    meshletResults[lod] = lodGen.buildMeshletsForLOD(submesh.lods[lod]);
                serializer.writeMeshletData(outFile, meshletResults);

                // Empty block (a single 0 byte the reader accepts) — V-HACD never runs. Generated
                // surfaces that need collision use ColliderShape::TriangleMesh, which reads the
                // LOD chain instead of hulls.
                serializer.writeConvexDecompositionData(outFile, resource::ConvexDecompositionData{});
            }

            // Static-mesh trailer: hasSkinning = 0, which also emits the socket block
            // (MeshSerializer.cpp:201-240). Every static .vfMesh ends this way.
            ExtractedSkeleton emptySkeleton;
            serializer.writeSkeletonData(outFile, emptySkeleton);

            outFile.close();
            if (!outFile)
            {
                result.message = "failed while writing";
                vfLogError("ProceduralMeshWriter: {} ({})", result.message, tempPath.string());
                std::filesystem::remove(tempPath, ec);
                return result;
            }
        }

        std::filesystem::rename(tempPath, finalPath, ec);
        if (ec)
        {
            result.message = "failed to replace the existing mesh: " + ec.message();
            vfLogError("ProceduralMeshWriter: {} ({})", result.message, outputPath);
            std::error_code cleanupEc;
            std::filesystem::remove(tempPath, cleanupEc);
            return result;
        }

        result.success = true;
        result.submeshCount = static_cast<uint32_t>(submeshes.size());
        vfLogInfo("Procedural mesh written: {} ({} submesh(es), {} LOD0 vertices)",
                  outputPath, result.submeshCount, result.totalVertices);
        return result;
    }
}
