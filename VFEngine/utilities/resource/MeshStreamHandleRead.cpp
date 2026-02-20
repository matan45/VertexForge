#include "MeshStreamHandle.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

namespace resource
{
    bool MeshStreamHandle::readMeshletData(uint32_t submeshIdx, SubmeshMeshletData& outMeshletData)
    {
        std::lock_guard<std::mutex> lock(fileMutex);

        if (!file.is_open())
        {
            vfLogError("MeshStreamHandle: File not open");
            return false;
        }

        if (!hasMeshlets)
        {
            vfLogError("MeshStreamHandle: File does not contain meshlet data");
            return false;
        }

        if (submeshIdx >= header.numSubmeshes)
        {
            vfLogError("MeshStreamHandle: Invalid submesh index {} (max {})",
                       submeshIdx, header.numSubmeshes);
            return false;
        }

        const auto& submeshInfo = header.submeshes[submeshIdx];
        if (!submeshInfo.hasMeshletData)
        {
            vfLogError("MeshStreamHandle: Submesh {} does not have meshlet data", submeshIdx);
            return false;
        }

        file.seekg(submeshInfo.meshletDataOffset);
        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to seek to meshlet data for submesh {}", submeshIdx);
            return false;
        }

        outMeshletData.name = submeshInfo.name;

        uint32_t totalMeshlets = 0;
        uint32_t totalVertexIndices = 0;
        uint32_t totalPrimitives = 0;

        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            uint32_t meshletCount = endian::readLE<uint32_t>(file);
            uint32_t vertexCount = endian::readLE<uint32_t>(file);
            uint32_t primitiveCount = endian::readLE<uint32_t>(file);

            outMeshletData.lodLevels[lod].meshletOffset = totalMeshlets;
            outMeshletData.lodLevels[lod].meshletCount = meshletCount;
            outMeshletData.lodLevels[lod].vertexDataOffset = totalVertexIndices;
            outMeshletData.lodLevels[lod].vertexDataCount = vertexCount;
            outMeshletData.lodLevels[lod].primitiveDataOffset = totalPrimitives;
            outMeshletData.lodLevels[lod].primitiveDataCount = primitiveCount;

            totalMeshlets += meshletCount;
            totalVertexIndices += vertexCount;
            totalPrimitives += primitiveCount;
        }

        outMeshletData.meshlets.resize(totalMeshlets);
        for (uint32_t i = 0; i < totalMeshlets; ++i)
        {
            auto& meshlet = outMeshletData.meshlets[i];

            meshlet.descriptor.vertexOffset = endian::readLE<uint32_t>(file);
            meshlet.descriptor.primitiveOffset = endian::readLE<uint32_t>(file);
            meshlet.descriptor.vertexCount = endian::readLE<uint8_t>(file);
            meshlet.descriptor.primitiveCount = endian::readLE<uint8_t>(file);
            meshlet.descriptor.padding = endian::readLE<uint16_t>(file);

            meshlet.bounds.boundingSphere.x = endian::readLE<float>(file);
            meshlet.bounds.boundingSphere.y = endian::readLE<float>(file);
            meshlet.bounds.boundingSphere.z = endian::readLE<float>(file);
            meshlet.bounds.boundingSphere.w = endian::readLE<float>(file);
            meshlet.bounds.cone.x = endian::readLE<float>(file);
            meshlet.bounds.cone.y = endian::readLE<float>(file);
            meshlet.bounds.cone.z = endian::readLE<float>(file);
            meshlet.bounds.cone.w = endian::readLE<float>(file);
        }

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read meshlet descriptors for submesh {}", submeshIdx);
            return false;
        }

        outMeshletData.meshletVertices.resize(totalVertexIndices);
        for (uint32_t i = 0; i < totalVertexIndices; ++i)
        {
            outMeshletData.meshletVertices[i] = endian::readLE<uint32_t>(file);
        }

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read meshlet vertices for submesh {}", submeshIdx);
            return false;
        }

        outMeshletData.meshletPrimitives.resize(totalPrimitives);
        for (uint32_t i = 0; i < totalPrimitives; ++i)
        {
            outMeshletData.meshletPrimitives[i] = endian::readLE<uint32_t>(file);
        }

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read meshlet primitives for submesh {}", submeshIdx);
            return false;
        }

        return true;
    }

    bool MeshStreamHandle::readConvexDecomposition(uint32_t submeshIdx, ConvexDecompositionData& outData)
    {
        std::lock_guard<std::mutex> lock(fileMutex);

        outData = ConvexDecompositionData{}; // Reset output

        if (!file.is_open())
        {
            vfLogError("MeshStreamHandle: File not open");
            return false;
        }

        if (!hasConvexHulls)
        {
            return true;
        }

        if (submeshIdx >= header.numSubmeshes)
        {
            vfLogError("MeshStreamHandle: Invalid submesh index {} (max {})",
                       submeshIdx, header.numSubmeshes);
            return false;
        }

        const auto& submeshInfo = header.submeshes[submeshIdx];
        if (!submeshInfo.hasConvexData)
        {
            return true;
        }

        file.seekg(submeshInfo.convexDataOffset);
        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to seek to convex data for submesh {}", submeshIdx);
            return false;
        }

        uint8_t hasDecomp = endian::readLE<uint8_t>(file);
        outData.hasDecomposition = (hasDecomp != 0);

        if (!outData.hasDecomposition)
        {
            return true;
        }

        outData.params.maxConvexHulls = endian::readLE<uint32_t>(file);
        outData.params.resolution = endian::readLE<uint32_t>(file);
        outData.params.maxVerticesPerHull = endian::readLE<uint32_t>(file);
        outData.params.minVolumePercentError = endian::readLE<float>(file);
        outData.params.maxRecursionDepth = endian::readLE<uint32_t>(file);

        uint32_t numHulls = endian::readLE<uint32_t>(file);
        if (numHulls > maxConvexHullCount)
        {
            vfLogError("MeshStreamHandle: Hull count {} exceeds limit {} in submesh {}",
                       numHulls, maxConvexHullCount, submeshIdx);
            return false;
        }

        outData.hulls.resize(numHulls);

        for (uint32_t h = 0; h < numHulls; ++h)
        {
            auto& hull = outData.hulls[h];

            uint32_t vertexCount = endian::readLE<uint32_t>(file);
            if (vertexCount > maxHullVertexCount)
            {
                vfLogError("MeshStreamHandle: Hull {} vertex count {} exceeds limit {} in submesh {}",
                           h, vertexCount, maxHullVertexCount, submeshIdx);
                return false;
            }

            hull.vertices.resize(vertexCount);
            for (uint32_t v = 0; v < vertexCount; ++v)
            {
                hull.vertices[v].x = endian::readLE<float>(file);
                hull.vertices[v].y = endian::readLE<float>(file);
                hull.vertices[v].z = endian::readLE<float>(file);
            }

            uint32_t indexCount = endian::readLE<uint32_t>(file);
            if (indexCount > maxHullIndexCount)
            {
                vfLogError("MeshStreamHandle: Hull {} index count {} exceeds limit {} in submesh {}",
                           h, indexCount, maxHullIndexCount, submeshIdx);
                return false;
            }

            hull.indices.resize(indexCount);
            for (uint32_t i = 0; i < indexCount; ++i)
            {
                hull.indices[i] = endian::readLE<uint32_t>(file);
            }

            hull.center.x = endian::readLE<float>(file);
            hull.center.y = endian::readLE<float>(file);
            hull.center.z = endian::readLE<float>(file);
            hull.volume = endian::readLE<float>(file);
        }

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read convex data for submesh {}", submeshIdx);
            return false;
        }

        return true;
    }

    bool MeshStreamHandle::readSkeleton(SkeletonData& outSkeleton)
    {
        std::lock_guard<std::mutex> lock(fileMutex);

        outSkeleton = SkeletonData{}; // Reset output

        if (!file.is_open())
        {
            vfLogError("MeshStreamHandle: File not open");
            return false;
        }

        if (!hasSkeleton)
        {
            return true; // No skeleton data is valid
        }

        file.seekg(header.skeletonDataOffset);
        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to seek to skeleton data");
            return false;
        }

        uint8_t hasSkinning = endian::readLE<uint8_t>(file);
        if (hasSkinning == 0)
        {
            return true;
        }

        uint32_t boneCount = endian::readLE<uint32_t>(file);
        outSkeleton.bones.resize(boneCount);
        outSkeleton.inverseBindPoses.resize(boneCount);

        // Read bone data
        for (uint32_t b = 0; b < boneCount; ++b)
        {
            auto& bone = outSkeleton.bones[b];

            // Read name
            uint32_t nameLength = endian::readLE<uint32_t>(file);
            if (nameLength > 0 && nameLength < 1024)
            {
                bone.name.resize(nameLength);
                file.read(bone.name.data(), nameLength);
            }

            // Read parent index
            bone.parentIndex = endian::readLE<int32_t>(file);

            // Read offset matrix
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    bone.offsetMatrix[col][row] = endian::readLE<float>(file);
                }
            }

            // Read pre-transform
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    bone.preTransform[col][row] = endian::readLE<float>(file);
                }
            }

            if (file.fail())
            {
                vfLogError("MeshStreamHandle: Failed to read bone {}", b);
                return false;
            }
        }

        // Read inverse bind poses
        for (uint32_t b = 0; b < boneCount; ++b)
        {
            auto& matrix = outSkeleton.inverseBindPoses[b];
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    matrix[col][row] = endian::readLE<float>(file);
                }
            }
        }

        // Read global inverse transform
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                outSkeleton.globalInverseTransform[col][row] = endian::readLE<float>(file);
            }
        }

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read skeleton data");
            return false;
        }

        // Compute bind poses (inverse of inverse bind poses = bone world transform at rest)
        outSkeleton.bindPoses.resize(boneCount);
        for (uint32_t b = 0; b < boneCount; ++b)
        {
            outSkeleton.bindPoses[b] = glm::inverse(outSkeleton.inverseBindPoses[b]);
        }

        // Read socket data if present
        if (hasSockets)
        {
            uint32_t socketCount = endian::readLE<uint32_t>(file);
            if (!file.fail() && socketCount < 256)
            {
                outSkeleton.sockets.resize(socketCount);
                for (uint32_t s = 0; s < socketCount; ++s)
                {
                    auto& socket = outSkeleton.sockets[s];

                    // Socket name
                    uint32_t nameLength = endian::readLE<uint32_t>(file);
                    if (nameLength > 0 && nameLength < 1024)
                    {
                        socket.name.resize(nameLength);
                        file.read(socket.name.data(), nameLength);
                    }

                    // Bone name
                    uint32_t boneNameLength = endian::readLE<uint32_t>(file);
                    if (boneNameLength > 0 && boneNameLength < 1024)
                    {
                        socket.targetBoneName.resize(boneNameLength);
                        file.read(socket.targetBoneName.data(), boneNameLength);
                    }

                    // Local position
                    socket.localPosition.x = endian::readLE<float>(file);
                    socket.localPosition.y = endian::readLE<float>(file);
                    socket.localPosition.z = endian::readLE<float>(file);

                    // Resolve bone index from name
                    socket.boneIndex = outSkeleton.getBoneIndex(socket.targetBoneName);

                    if (file.fail())
                    {
                        vfLogError("MeshStreamHandle: Failed to read socket {}", s);
                        outSkeleton.sockets.clear();
                        return false;
                    }
                }

                vfLogInfo("MeshStreamHandle: Loaded {} sockets", socketCount);
            }
        }

        vfLogInfo("MeshStreamHandle: Loaded skeleton with {} bones", boneCount);
        return true;
    }
}
