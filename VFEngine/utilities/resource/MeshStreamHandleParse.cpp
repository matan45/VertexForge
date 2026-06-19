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

        if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
        {
            vfLogError("MeshStreamHandle: Incompatible mesh file version: {}.{}.{}, expected {}.{}.{}. Re-import required.",
                       majorVersion, minorVersion, patchVersion,
                       Version::major, Version::minor, Version::patch);
            return false;
        }

        hasMeshlets = true;
        hasConvexHulls = true;

        header.numSubmeshes = endian::readLE<uint32_t>(file);

        compressionFlags = endian::readLE<uint32_t>(file);
        header.compressionFlags = compressionFlags;

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
                    lodInfo.fileOffset = file.tellg() - baseOffset;

                    lodInfo.vertexCount = endian::readLE<uint32_t>(file);
                    if (lodInfo.vertexCount > maxVertexCount)
                    {
                        vfLogError("MeshStreamHandle: Vertex count {} exceeds limit in submesh {} LOD {}",
                                   lodInfo.vertexCount, meshIdx, lodIdx);
                        return false;
                    }

                    if (compressionFlags != 0)
                    {
                        // Compressed format: skip AABB (24B) + read vertex blob size + skip blob
                        file.seekg(24, std::ios::cur); // AABB: 6 floats
                        lodInfo.encodedVertexBlobSize = endian::readLE<uint32_t>(file);
                        file.seekg(lodInfo.encodedVertexBlobSize, std::ios::cur);

                        lodInfo.indexCount = endian::readLE<uint32_t>(file);
                        if (lodInfo.indexCount > maxIndexCount)
                        {
                            vfLogError("MeshStreamHandle: Index count {} exceeds limit in submesh {} LOD {}",
                                       lodInfo.indexCount, meshIdx, lodIdx);
                            return false;
                        }

                        lodInfo.encodedIndexBlobSize = endian::readLE<uint32_t>(file);
                        file.seekg(lodInfo.encodedIndexBlobSize, std::ios::cur);
                    }
                    else
                    {
                        // Uncompressed format (legacy — won't be hit with version 1.0.1)
                        file.seekg(lodInfo.vertexCount * 64, std::ios::cur);

                        lodInfo.indexCount = endian::readLE<uint32_t>(file);
                        if (lodInfo.indexCount > maxIndexCount)
                        {
                            vfLogError("MeshStreamHandle: Index count {} exceeds limit in submesh {} LOD {}",
                                       lodInfo.indexCount, meshIdx, lodIdx);
                            return false;
                        }

                        file.seekg(lodInfo.indexCount * sizeof(uint32_t), std::ios::cur);
                    }

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

            if (!parseMeshletHeaders(meshIdx))
            {
                return false;
            }

            if (!parseConvexHeaders(meshIdx))
            {
                return false;
            }
        }

        if (!parseSkeletonHeader())
        {
            return false;
        }

        return true;
    }

    bool MeshStreamHandle::parseMeshletHeaders(uint32_t meshIdx)
    {
        auto& submeshInfo = header.submeshes[meshIdx];

        submeshInfo.meshletDataOffset = file.tellg() - baseOffset;
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

        submeshInfo.convexDataOffset = file.tellg() - baseOffset;

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
        header.skeletonDataOffset = file.tellg() - baseOffset;

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
        socketDataOffset = file.tellg() - baseOffset;
        std::streampos beforeSockets = socketDataOffset;

        // Versioned socket block (VK-1402): a versioned block begins with a magic sentinel
        // (>> 256 so it cannot be mistaken for a legacy count) followed by a version, then
        // the count. Version 2 stores a per-socket localRotation (4 floats) after the
        // position. Legacy blocks begin directly with the count. Keep this skip logic in
        // sync with MeshStreamHandle::readSocketDefinitions.
        constexpr uint32_t kSocketBlockMagic = 0x534F4B32; // 'SOK2'
        uint32_t firstWord = endian::readLE<uint32_t>(file);
        uint32_t socketBlockVersion = 1;
        uint32_t socketCount = firstWord;
        if (!file.fail() && firstWord == kSocketBlockMagic)
        {
            socketBlockVersion = endian::readLE<uint32_t>(file);
            socketCount = endian::readLE<uint32_t>(file);
        }

        if (!file.fail() && socketCount < 256)
        {
            hasSockets = true;
            const std::streamoff rotationBytes =
                (socketBlockVersion >= 2) ? static_cast<std::streamoff>(4 * sizeof(float)) : 0;
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

                // position(3) = 3 floats, plus rotation(4) floats for version >= 2
                file.seekg(3 * sizeof(float) + rotationBytes, std::ios::cur);

                if (file.fail()) { hasSockets = false; break; }
            }
        }
        else
        {
            // No socket data or EOF — backward compatible
            file.clear();
            file.seekg(baseOffset + beforeSockets);
            hasSockets = false;
        }

        // Try to read IK chain data (appended after sockets)
        ikChainDataOffset = file.tellg() - baseOffset;
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
            file.seekg(baseOffset + beforeIKChains);
            hasIKChains = false;
        }

        return true;
    }
}
