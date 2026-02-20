#include "MeshSerializer.hpp"
#include "print/EditorLogger.hpp"
#include "resource/EndianUtils.hpp"

#include <algorithm>

namespace
{
    void writeMatrix(std::ofstream& outFile, const glm::mat4& matrix)
    {
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                resource::endian::writeLE<float>(outFile, matrix[col][row]);
            }
        }
    }
}

namespace types
{
    void MeshSerializer::writeLODLevel(std::ofstream& outFile, const LODMeshData& lodMesh) const
    {
        constexpr size_t verticesPerChunk = chunkSize / sizeof(resource::Vertex);

        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(lodMesh.vertices.size()));

        for (size_t v = 0; v < lodMesh.vertices.size(); v += verticesPerChunk)
        {
            size_t chunkEnd = std::min(v + verticesPerChunk, lodMesh.vertices.size());

            for (size_t j = v; j < chunkEnd; ++j)
            {
                const auto& vertex = lodMesh.vertices[j];
                resource::endian::writeLE<float>(outFile, vertex.position.x);
                resource::endian::writeLE<float>(outFile, vertex.position.y);
                resource::endian::writeLE<float>(outFile, vertex.position.z);
                resource::endian::writeLE<float>(outFile, vertex.normal.x);
                resource::endian::writeLE<float>(outFile, vertex.normal.y);
                resource::endian::writeLE<float>(outFile, vertex.normal.z);
                resource::endian::writeLE<float>(outFile, vertex.texCoords.x);
                resource::endian::writeLE<float>(outFile, vertex.texCoords.y);
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.x);
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.y);
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.z);
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.w);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.x);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.y);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.z);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.w);
            }
        }

        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(lodMesh.indices.size()));

        constexpr size_t indicesPerChunk = chunkSize / sizeof(uint32_t);
        for (size_t i = 0; i < lodMesh.indices.size(); i += indicesPerChunk)
        {
            size_t chunkEnd = std::min(i + indicesPerChunk, lodMesh.indices.size());
            std::vector<uint32_t> indexChunk(lodMesh.indices.begin() + i, lodMesh.indices.begin() + chunkEnd);
            resource::endian::writeVectorLE<uint32_t>(outFile, indexChunk);
        }
    }

    void MeshSerializer::writeMeshletData(std::ofstream& outFile,
                                          const std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT>& meshletResults) const
    {
        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            const auto& result = meshletResults[lod];
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshlets.size()));
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshletVertices.size()));
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshletPrimitives.size()));
        }

        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (const auto& meshlet : meshletResults[lod].meshlets)
            {
                resource::endian::writeLE<uint32_t>(outFile, meshlet.descriptor.vertexOffset);
                resource::endian::writeLE<uint32_t>(outFile, meshlet.descriptor.primitiveOffset);
                resource::endian::writeLE<uint8_t>(outFile, meshlet.descriptor.vertexCount);
                resource::endian::writeLE<uint8_t>(outFile, meshlet.descriptor.primitiveCount);
                resource::endian::writeLE<uint16_t>(outFile, 0); // padding

                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.x);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.y);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.z);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.w);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.x);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.y);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.z);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.w);
            }
        }

        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (uint32_t idx : meshletResults[lod].meshletVertices)
            {
                resource::endian::writeLE<uint32_t>(outFile, idx);
            }
        }

        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (uint32_t packed : meshletResults[lod].meshletPrimitives)
            {
                resource::endian::writeLE<uint32_t>(outFile, packed);
            }
        }
    }

    void MeshSerializer::writeConvexDecompositionData(std::ofstream& outFile,
                                                      const resource::ConvexDecompositionData& decomposition) const
    {
        resource::endian::writeLE<uint8_t>(outFile, decomposition.hasDecomposition ? 1 : 0);

        if (!decomposition.hasDecomposition)
        {
            return;
        }

        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxConvexHulls);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.resolution);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxVerticesPerHull);
        resource::endian::writeLE<float>(outFile, decomposition.params.minVolumePercentError);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxRecursionDepth);

        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(decomposition.hulls.size()));

        for (const auto& hull : decomposition.hulls)
        {
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(hull.vertices.size()));
            for (const auto& v : hull.vertices)
            {
                resource::endian::writeLE<float>(outFile, v.x);
                resource::endian::writeLE<float>(outFile, v.y);
                resource::endian::writeLE<float>(outFile, v.z);
            }

            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(hull.indices.size()));
            for (uint32_t idx : hull.indices)
            {
                resource::endian::writeLE<uint32_t>(outFile, idx);
            }

            resource::endian::writeLE<float>(outFile, hull.center.x);
            resource::endian::writeLE<float>(outFile, hull.center.y);
            resource::endian::writeLE<float>(outFile, hull.center.z);
            resource::endian::writeLE<float>(outFile, hull.volume);
        }
    }

    void MeshSerializer::writeSkeletonData(std::ofstream& outFile, const ExtractedSkeleton& skeleton) const
    {
        resource::endian::writeLE<uint8_t>(outFile, skeleton.hasSkinning ? 1 : 0);

        if (!skeleton.hasSkinning)
        {
            return;
        }

        uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
        resource::endian::writeLE<uint32_t>(outFile, boneCount);

        for (const auto& bone : skeleton.bones)
        {
            uint32_t nameLength = static_cast<uint32_t>(bone.name.length());
            resource::endian::writeLE<uint32_t>(outFile, nameLength);
            if (nameLength > 0)
            {
                outFile.write(bone.name.data(), nameLength);
            }

            resource::endian::writeLE<int32_t>(outFile, bone.parentIndex);
            writeMatrix(outFile, bone.offsetMatrix);
            writeMatrix(outFile, bone.preTransform);
        }

        for (const auto& matrix : skeleton.inverseBindPoses)
        {
            writeMatrix(outFile, matrix);
        }

        writeMatrix(outFile, skeleton.globalInverseTransform);

        // Write socket data after skeleton
        writeSocketData(outFile, skeleton.sockets);

        vfLogInfo("Written full skeleton data: {} bones with hierarchy, {} sockets", boneCount, skeleton.sockets.size());
    }

    void MeshSerializer::writeSocketData(std::ofstream& outFile, const std::vector<animator::SocketDefinition>& sockets) const
    {
        uint32_t socketCount = static_cast<uint32_t>(sockets.size());
        resource::endian::writeLE<uint32_t>(outFile, socketCount);

        for (const auto& socket : sockets)
        {
            // Socket name
            uint32_t nameLength = static_cast<uint32_t>(socket.name.length());
            resource::endian::writeLE<uint32_t>(outFile, nameLength);
            if (nameLength > 0)
            {
                outFile.write(socket.name.data(), nameLength);
            }

            // Target bone name
            uint32_t boneNameLength = static_cast<uint32_t>(socket.targetBoneName.length());
            resource::endian::writeLE<uint32_t>(outFile, boneNameLength);
            if (boneNameLength > 0)
            {
                outFile.write(socket.targetBoneName.data(), boneNameLength);
            }

            // Local position
            resource::endian::writeLE<float>(outFile, socket.localPosition.x);
            resource::endian::writeLE<float>(outFile, socket.localPosition.y);
            resource::endian::writeLE<float>(outFile, socket.localPosition.z);

            // Local rotation (quaternion w,x,y,z)
            resource::endian::writeLE<float>(outFile, socket.localRotation.w);
            resource::endian::writeLE<float>(outFile, socket.localRotation.x);
            resource::endian::writeLE<float>(outFile, socket.localRotation.y);
            resource::endian::writeLE<float>(outFile, socket.localRotation.z);

            // Local scale
            resource::endian::writeLE<float>(outFile, socket.localScale.x);
            resource::endian::writeLE<float>(outFile, socket.localScale.y);
            resource::endian::writeLE<float>(outFile, socket.localScale.z);
        }
    }
}
