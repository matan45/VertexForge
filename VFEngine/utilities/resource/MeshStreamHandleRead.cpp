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

        outSkeleton = SkeletonData{};

        if (!file.is_open())
        {
            vfLogError("MeshStreamHandle: File not open");
            return false;
        }

        if (!hasSkeleton)
        {
            return true;
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

        if (!readBoneHierarchy(boneCount, outSkeleton))
            return false;

        if (!readBindPoseData(boneCount, outSkeleton))
            return false;

        if (hasSockets && !readSocketDefinitions(outSkeleton))
            return false;

        if (hasIKChains && !readIKChainDefinitions(outSkeleton))
            return false;

        vfLogInfo("MeshStreamHandle: Loaded skeleton with {} bones", boneCount);
        return true;
    }

    bool MeshStreamHandle::readBoneHierarchy(uint32_t boneCount, SkeletonData& outSkeleton)
    {
        for (uint32_t b = 0; b < boneCount; ++b)
        {
            auto& bone = outSkeleton.bones[b];

            uint32_t nameLength = endian::readLE<uint32_t>(file);
            if (nameLength > 0 && nameLength < 1024)
            {
                bone.name.resize(nameLength);
                file.read(bone.name.data(), nameLength);
            }

            bone.parentIndex = endian::readLE<int32_t>(file);

            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    bone.offsetMatrix[col][row] = endian::readLE<float>(file);

            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    bone.preTransform[col][row] = endian::readLE<float>(file);

            if (file.fail())
            {
                vfLogError("MeshStreamHandle: Failed to read bone {}", b);
                return false;
            }
        }

        return true;
    }

    bool MeshStreamHandle::readBindPoseData(uint32_t boneCount, SkeletonData& outSkeleton)
    {
        for (uint32_t b = 0; b < boneCount; ++b)
        {
            auto& matrix = outSkeleton.inverseBindPoses[b];
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    matrix[col][row] = endian::readLE<float>(file);
        }

        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                outSkeleton.globalInverseTransform[col][row] = endian::readLE<float>(file);

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read skeleton data");
            return false;
        }

        outSkeleton.bindPoses.resize(boneCount);
        for (uint32_t b = 0; b < boneCount; ++b)
        {
            outSkeleton.bindPoses[b] = glm::inverse(outSkeleton.inverseBindPoses[b]);
        }

        return true;
    }

    bool MeshStreamHandle::readSocketDefinitions(SkeletonData& outSkeleton)
    {
        uint32_t socketCount = endian::readLE<uint32_t>(file);
        if (file.fail() || socketCount >= 256)
        {
            return true;
        }

        outSkeleton.sockets.resize(socketCount);
        for (uint32_t s = 0; s < socketCount; ++s)
        {
            auto& socket = outSkeleton.sockets[s];

            uint32_t nameLength = endian::readLE<uint32_t>(file);
            if (nameLength > 0 && nameLength < 1024)
            {
                socket.name.resize(nameLength);
                file.read(socket.name.data(), nameLength);
            }

            uint32_t boneNameLength = endian::readLE<uint32_t>(file);
            if (boneNameLength > 0 && boneNameLength < 1024)
            {
                socket.targetBoneName.resize(boneNameLength);
                file.read(socket.targetBoneName.data(), boneNameLength);
            }

            socket.localPosition.x = endian::readLE<float>(file);
            socket.localPosition.y = endian::readLE<float>(file);
            socket.localPosition.z = endian::readLE<float>(file);

            socket.boneIndex = outSkeleton.getBoneIndex(socket.targetBoneName);

            if (file.fail())
            {
                vfLogError("MeshStreamHandle: Failed to read socket {}", s);
                outSkeleton.sockets.clear();
                return false;
            }
        }

        vfLogInfo("MeshStreamHandle: Loaded {} sockets", socketCount);
        return true;
    }

    bool MeshStreamHandle::readIKChainDefinitions(SkeletonData& outSkeleton)
    {
        uint32_t chainCount = endian::readLE<uint32_t>(file);
        if (file.fail() || chainCount >= 256)
        {
            return true;
        }

        outSkeleton.ikChains.resize(chainCount);
        for (uint32_t c = 0; c < chainCount; ++c)
        {
            auto& chain = outSkeleton.ikChains[c];

            // Chain name
            uint32_t nameLen = endian::readLE<uint32_t>(file);
            if (nameLen > 0 && nameLen < 1024)
            {
                chain.chainName.resize(nameLen);
                file.read(chain.chainName.data(), nameLen);
            }

            // Tip bone name
            uint32_t tipLen = endian::readLE<uint32_t>(file);
            if (tipLen > 0 && tipLen < 1024)
            {
                chain.tipBoneName.resize(tipLen);
                file.read(chain.tipBoneName.data(), tipLen);
            }

            // Chain bone names
            uint32_t boneCount = endian::readLE<uint32_t>(file);
            chain.chainBoneNames.resize(boneCount);
            for (uint32_t b = 0; b < boneCount; ++b)
            {
                uint32_t bLen = endian::readLE<uint32_t>(file);
                if (bLen > 0 && bLen < 1024)
                {
                    chain.chainBoneNames[b].resize(bLen);
                    file.read(chain.chainBoneNames[b].data(), bLen);
                }
            }

            // Constraints
            uint32_t constraintCount = endian::readLE<uint32_t>(file);
            chain.constraints.resize(constraintCount);
            for (uint32_t k = 0; k < constraintCount; ++k)
            {
                auto& constraint = chain.constraints[k];
                constraint.type = static_cast<animator::ik::JointConstraintType>(endian::readLE<uint8_t>(file));
                constraint.hingeAxis.x = endian::readLE<float>(file);
                constraint.hingeAxis.y = endian::readLE<float>(file);
                constraint.hingeAxis.z = endian::readLE<float>(file);
                constraint.coneAngle = endian::readLE<float>(file);
                constraint.swingAngle = endian::readLE<float>(file);
                constraint.twistMin = endian::readLE<float>(file);
                constraint.twistMax = endian::readLE<float>(file);
            }

            // Weight and enabled
            chain.weight = endian::readLE<float>(file);
            chain.enabled = (endian::readLE<uint8_t>(file) != 0);

            if (file.fail())
            {
                vfLogError("MeshStreamHandle: Failed to read IK chain {}", c);
                outSkeleton.ikChains.clear();
                return false;
            }
        }

        vfLogInfo("MeshStreamHandle: Loaded {} IK chains", chainCount);
        return true;
    }
}
