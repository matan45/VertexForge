#include "MeshStreamHandle.hpp"
#include "../print/Log.hpp"
#include "EndianUtils.hpp"
#include <filesystem>

namespace resource
{
    bool MeshStreamHandle::parseHeader()
    {
        if (!file.is_open()) return false;

        uint8_t headerFileType = endian::readLE<uint8_t>(file);
        header.headerFileType = static_cast<FileType>(headerFileType);

        uint32_t majorVersion = endian::readLE<uint32_t>(file);
        uint32_t minorVersion = endian::readLE<uint32_t>(file);
        uint32_t patchVersion = endian::readLE<uint32_t>(file);

        header.version.major = majorVersion;
        header.version.minor = minorVersion;
        header.version.patch = patchVersion;

        bool isLODFormat = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 3);
        if (!isLODFormat)
        {
            vfLogError("MeshStreamHandle: Incompatible mesh file version: {}.{}.{}",
                       majorVersion, minorVersion, patchVersion);
            return false;
        }

        hasMeshlets = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 4);
        hasConvexHulls = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 5);
        // Version 0.0.7+ uses 64-byte vertices (with bone data), older versions use 32-byte
        has64ByteVertices = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 7);

        header.numSubmeshes = endian::readLE<uint32_t>(file);

        // Version 0.0.7+ has a skeleton reference field after numSubmeshes
        if (has64ByteVertices)
        {
            uint32_t skeletonRefLength = endian::readLE<uint32_t>(file);
            if (skeletonRefLength > 0)
            {
                // Skip skeleton reference path string if present
                file.seekg(skeletonRefLength, std::ios::cur);
            }
        }

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read header");
            return false;
        }

        if (header.numSubmeshes > maxSubmeshCount)
        {
            vfLogError("MeshStreamHandle: Submesh count {} exceeds limit {}",
                       header.numSubmeshes, maxSubmeshCount);
            return false;
        }

        header.submeshes.resize(header.numSubmeshes);

        for (uint32_t meshIdx = 0; meshIdx < header.numSubmeshes; ++meshIdx)
        {
            auto& submeshInfo = header.submeshes[meshIdx];

            uint32_t nameLength = endian::readLE<uint32_t>(file);
            if (nameLength > 0 && nameLength < 1024)
            {
                submeshInfo.name.resize(nameLength);
                file.read(submeshInfo.name.data(), nameLength);
            }
            else if (nameLength == 0)
            {
                submeshInfo.name = "SubMesh_" + std::to_string(meshIdx);
            }
            else
            {
                vfLogError("MeshStreamHandle: Invalid name length {} for submesh {}",
                           nameLength, meshIdx);
                return false;
            }

            uint32_t lodLevelCount = endian::readLE<uint32_t>(file);
            if (lodLevelCount == 0 || lodLevelCount > 8)
            {
                vfLogError("MeshStreamHandle: Invalid LOD level count {} in submesh {}",
                           lodLevelCount, meshIdx);
                return false;
            }

            for (uint32_t lodIdx = 0; lodIdx < LOD_LEVEL_COUNT; ++lodIdx)
            {
                auto& lodInfo = submeshInfo.lods[lodIdx];

                if (lodIdx < lodLevelCount)
                {
                    lodInfo.fileOffset = file.tellg();

                    lodInfo.vertexCount = endian::readLE<uint32_t>(file);
                    if (lodInfo.vertexCount > maxVertexCount)
                    {
                        vfLogError("MeshStreamHandle: Vertex count {} exceeds limit in submesh {} LOD {}",
                                   lodInfo.vertexCount, meshIdx, lodIdx);
                        return false;
                    }

                    // Vertex size depends on file version: 64 bytes for v0.0.7+ (with bone data), 32 bytes for older
                    size_t vertexSize = has64ByteVertices ? 64 : 32;
                    file.seekg(lodInfo.vertexCount * vertexSize, std::ios::cur);

                    lodInfo.indexCount = endian::readLE<uint32_t>(file);
                    if (lodInfo.indexCount > maxIndexCount)
                    {
                        vfLogError("MeshStreamHandle: Index count {} exceeds limit in submesh {} LOD {}",
                                   lodInfo.indexCount, meshIdx, lodIdx);
                        return false;
                    }

                    file.seekg(lodInfo.indexCount * sizeof(uint32_t), std::ios::cur);

                    if (lodInfo.vertexCount == 0 && lodIdx > 0)
                    {
                        lodInfo = submeshInfo.lods[lodIdx - 1];
                    }
                }
                else
                {
                    uint32_t lastLod = lodLevelCount - 1;
                    lodInfo = submeshInfo.lods[lastLod];
                }

                if (file.fail())
                {
                    vfLogError("MeshStreamHandle: Failed to parse LOD {} of submesh {}",
                               lodIdx, meshIdx);
                    return false;
                }
            }

            if (hasMeshlets)
            {
                if (!parseMeshletHeaders(meshIdx))
                {
                    return false;
                }
            }

            if (hasConvexHulls)
            {
                if (!parseConvexHeaders(meshIdx))
                {
                    return false;
                }
            }
        }

        // Parse skeleton data header (v0.0.7+)
        if (has64ByteVertices)
        {
            if (!parseSkeletonHeader())
            {
                return false;
            }
        }

        return true;
    }

    bool MeshStreamHandle::parseMeshletHeaders(uint32_t meshIdx)
    {
        auto& submeshInfo = header.submeshes[meshIdx];

        submeshInfo.meshletDataOffset = file.tellg();
        submeshInfo.hasMeshletData = true;

        uint32_t totalMeshlets = 0;
        uint32_t totalVertexIndices = 0;
        uint32_t totalPrimitives = 0;

        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            auto& meshletInfo = submeshInfo.meshletLods[lod];
            meshletInfo.meshletCount = endian::readLE<uint32_t>(file);
            meshletInfo.vertexIndexCount = endian::readLE<uint32_t>(file);
            meshletInfo.primitiveCount = endian::readLE<uint32_t>(file);

            if (meshletInfo.meshletCount > maxMeshletCount)
            {
                vfLogError("MeshStreamHandle: Meshlet count {} exceeds limit {} in submesh {} LOD {}",
                           meshletInfo.meshletCount, maxMeshletCount, meshIdx, lod);
                return false;
            }

            totalMeshlets += meshletInfo.meshletCount;
            totalVertexIndices += meshletInfo.vertexIndexCount;
            totalPrimitives += meshletInfo.primitiveCount;
        }

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read meshlet headers for submesh {}", meshIdx);
            return false;
        }

        file.seekg(totalMeshlets * sizeof(Meshlet), std::ios::cur);
        file.seekg(totalVertexIndices * sizeof(uint32_t), std::ios::cur);
        file.seekg(totalPrimitives * sizeof(uint32_t), std::ios::cur);

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to skip meshlet data for submesh {}", meshIdx);
            return false;
        }

        return true;
    }

    bool MeshStreamHandle::parseConvexHeaders(uint32_t meshIdx)
    {
        auto& submeshInfo = header.submeshes[meshIdx];

        submeshInfo.convexDataOffset = file.tellg();

        uint8_t hasDecomp = endian::readLE<uint8_t>(file);
        submeshInfo.hasConvexData = (hasDecomp != 0);

        if (!submeshInfo.hasConvexData)
        {
            return !file.fail();
        }

        file.seekg(20, std::ios::cur); // Skip parameters

        uint32_t numHulls = endian::readLE<uint32_t>(file);
        if (numHulls > maxConvexHullCount)
        {
            vfLogError("MeshStreamHandle: Hull count {} exceeds limit {} in submesh {}",
                       numHulls, maxConvexHullCount, meshIdx);
            return false;
        }

        for (uint32_t h = 0; h < numHulls; ++h)
        {
            uint32_t vertexCount = endian::readLE<uint32_t>(file);
            if (vertexCount > maxHullVertexCount)
            {
                vfLogError("MeshStreamHandle: Hull {} vertex count {} exceeds limit {} in submesh {}",
                           h, vertexCount, maxHullVertexCount, meshIdx);
                return false;
            }
            file.seekg(vertexCount * 3 * sizeof(float), std::ios::cur);

            uint32_t indexCount = endian::readLE<uint32_t>(file);
            if (indexCount > maxHullIndexCount)
            {
                vfLogError("MeshStreamHandle: Hull {} index count {} exceeds limit {} in submesh {}",
                           h, indexCount, maxHullIndexCount, meshIdx);
                return false;
            }
            file.seekg(indexCount * sizeof(uint32_t), std::ios::cur);
            file.seekg(4 * sizeof(float), std::ios::cur); // center + volume
        }

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to parse convex data for submesh {}", meshIdx);
            return false;
        }

        return true;
    }

    bool MeshStreamHandle::parseSkeletonHeader()
    {
        header.skeletonDataOffset = file.tellg();

        uint8_t hasSkinning = endian::readLE<uint8_t>(file);
        hasSkeleton = (hasSkinning != 0);

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read skeleton header");
            return false;
        }

        if (!hasSkeleton)
        {
            return true;
        }

        // Skip over skeleton data for header parsing
        uint32_t boneCount = endian::readLE<uint32_t>(file);
        if (boneCount > 1000)
        {
            vfLogError("MeshStreamHandle: Invalid bone count {}", boneCount);
            return false;
        }

        // Skip bone data
        for (uint32_t b = 0; b < boneCount; ++b)
        {
            uint32_t nameLength = endian::readLE<uint32_t>(file);
            if (nameLength > 1024)
            {
                vfLogError("MeshStreamHandle: Invalid bone name length {}", nameLength);
                return false;
            }
            file.seekg(nameLength, std::ios::cur); // Skip name
            file.seekg(4, std::ios::cur); // parentIndex
            file.seekg(16 * 4, std::ios::cur); // offsetMatrix
            file.seekg(16 * 4, std::ios::cur); // preTransform
        }

        // Skip inverse bind poses (boneCount matrices)
        file.seekg(boneCount * 16 * sizeof(float), std::ios::cur);

        // Skip global inverse transform
        file.seekg(16 * sizeof(float), std::ios::cur);

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to skip skeleton data");
            return false;
        }

        // Try to read socket data (appended after skeleton)
        socketDataOffset = file.tellg();
        std::streampos beforeSockets = socketDataOffset;
        uint32_t socketCount = endian::readLE<uint32_t>(file);
        if (!file.fail() && socketCount < 256)
        {
            hasSockets = true;
            // Skip socket data for header parsing
            for (uint32_t s = 0; s < socketCount; ++s)
            {
                // Socket name
                uint32_t nameLength = endian::readLE<uint32_t>(file);
                if (nameLength > 1024 || file.fail()) { hasSockets = false; break; }
                file.seekg(nameLength, std::ios::cur);

                // Bone name
                uint32_t boneNameLength = endian::readLE<uint32_t>(file);
                if (boneNameLength > 1024 || file.fail()) { hasSockets = false; break; }
                file.seekg(boneNameLength, std::ios::cur);

                // position(3) = 3 floats
                file.seekg(3 * sizeof(float), std::ios::cur);

                if (file.fail()) { hasSockets = false; break; }
            }
        }
        else
        {
            // No socket data or EOF — backward compatible
            file.clear();
            file.seekg(beforeSockets);
            hasSockets = false;
        }

        // Try to read IK chain data (appended after sockets)
        ikChainDataOffset = file.tellg();
        std::streampos beforeIKChains = ikChainDataOffset;
        uint32_t ikChainCount = endian::readLE<uint32_t>(file);
        if (!file.fail() && ikChainCount < 256)
        {
            hasIKChains = true;
            // Skip IK chain data for header parsing
            for (uint32_t c = 0; c < ikChainCount; ++c)
            {
                // Chain name
                uint32_t nameLen = endian::readLE<uint32_t>(file);
                if (nameLen > 1024 || file.fail()) { hasIKChains = false; break; }
                file.seekg(nameLen, std::ios::cur);

                // Tip bone name
                uint32_t tipLen = endian::readLE<uint32_t>(file);
                if (tipLen > 1024 || file.fail()) { hasIKChains = false; break; }
                file.seekg(tipLen, std::ios::cur);

                // Chain bone names
                uint32_t boneCount = endian::readLE<uint32_t>(file);
                if (boneCount > 256 || file.fail()) { hasIKChains = false; break; }
                for (uint32_t b = 0; b < boneCount; ++b)
                {
                    uint32_t bLen = endian::readLE<uint32_t>(file);
                    if (bLen > 1024 || file.fail()) { hasIKChains = false; break; }
                    file.seekg(bLen, std::ios::cur);
                }
                if (!hasIKChains) break;

                // Constraints
                uint32_t constraintCount = endian::readLE<uint32_t>(file);
                if (constraintCount > 256 || file.fail()) { hasIKChains = false; break; }
                // Each constraint: type(1) + hingeAxis(12) + coneAngle(4) + swingAngle(4) + twistMin(4) + twistMax(4) = 29 bytes
                file.seekg(constraintCount * 29, std::ios::cur);

                // weight(4) + enabled(1)
                file.seekg(5, std::ios::cur);

                if (file.fail()) { hasIKChains = false; break; }
            }
        }
        else
        {
            file.clear();
            file.seekg(beforeIKChains);
            hasIKChains = false;
        }

        return true;
    }
}
