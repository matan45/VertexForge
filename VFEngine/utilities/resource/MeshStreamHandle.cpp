#include "MeshStreamHandle.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"
#include <filesystem>

namespace resource
{
    MeshStreamHandle::~MeshStreamHandle()
    {
        close();
    }

    MeshStreamHandle::MeshStreamHandle(MeshStreamHandle&& other) noexcept
        : file(std::move(other.file))
          , header(std::move(other.header))
          , filePath(std::move(other.filePath))
    {
    }

    MeshStreamHandle& MeshStreamHandle::operator=(MeshStreamHandle&& other) noexcept
    {
        if (this != &other)
        {
            close();
            file = std::move(other.file);
            header = std::move(other.header);
            filePath = std::move(other.filePath);
        }
        return *this;
    }

    bool MeshStreamHandle::openStream(std::string_view path)
    {
        close();

        filePath = std::string(path);
        file.open(filePath, std::ios::binary);

        if (!file)
        {
            vfLogError("MeshStreamHandle: Failed to open mesh file: {}", path);
            return false;
        }

        if (!parseHeader())
        {
            vfLogError("MeshStreamHandle: Failed to parse header: {}", path);
            close();
            return false;
        }

        return true;
    }

    void MeshStreamHandle::close()
    {
        if (file.is_open())
        {
            file.close();
        }
        header = MeshStreamHeader{};
        filePath.clear();
    }

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
        hasClusterDAGs = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 8);

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

            if (hasClusterDAGs)
            {
                if (!parseClusterDAGHeaders(meshIdx))
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

    bool MeshStreamHandle::parseClusterDAGHeaders(uint32_t meshIdx)
    {
        auto& submeshInfo = header.submeshes[meshIdx];

        submeshInfo.clusterDAGDataOffset = file.tellg();

        uint8_t hasDAG = endian::readLE<uint8_t>(file);
        submeshInfo.hasClusterDAGData = (hasDAG != 0);

        if (!submeshInfo.hasClusterDAGData)
        {
            return !file.fail();
        }

        auto& dagInfo = submeshInfo.clusterDAGInfo;
        dagInfo.clusterCount = endian::readLE<uint32_t>(file);

        if (dagInfo.clusterCount > maxClusterCount)
        {
            vfLogError("MeshStreamHandle: Cluster count {} exceeds limit {} in submesh {}",
                       dagInfo.clusterCount, maxClusterCount, meshIdx);
            return false;
        }

        dagInfo.leafClusterCount = endian::readLE<uint32_t>(file);
        dagInfo.maxDepth = endian::readLE<uint32_t>(file);
        dagInfo.maxGeometricError = endian::readLE<float>(file);

        dagInfo.boundingSphere.x = endian::readLE<float>(file);
        dagInfo.boundingSphere.y = endian::readLE<float>(file);
        dagInfo.boundingSphere.z = endian::readLE<float>(file);
        dagInfo.boundingSphere.w = endian::readLE<float>(file);

        // Skip cluster data (64 bytes per cluster)
        file.seekg(dagInfo.clusterCount * 64, std::ios::cur);

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to parse cluster DAG data for submesh {}", meshIdx);
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

        vfLogInfo("MeshStreamHandle: Loaded skeleton with {} bones", boneCount);
        return true;
    }

    bool MeshStreamHandle::readLODLevel(uint32_t submeshIdx, uint32_t lodLevel,
                                        std::vector<Vertex>& outVertices,
                                        std::vector<uint32_t>& outIndices)
    {
        std::lock_guard<std::mutex> lock(fileMutex);

        if (!file.is_open())
        {
            vfLogError("MeshStreamHandle: File not open");
            return false;
        }

        if (submeshIdx >= header.numSubmeshes)
        {
            vfLogError("MeshStreamHandle: Invalid submesh index {} (max {})",
                       submeshIdx, header.numSubmeshes);
            return false;
        }

        if (lodLevel >= LOD_LEVEL_COUNT)
        {
            vfLogError("MeshStreamHandle: Invalid LOD level {}", lodLevel);
            return false;
        }

        const auto& lodInfo = header.submeshes[submeshIdx].lods[lodLevel];

        file.seekg(lodInfo.fileOffset);
        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to seek to LOD {} of submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        uint32_t vertexCount = endian::readLE<uint32_t>(file);
        if (vertexCount != lodInfo.vertexCount)
        {
            vfLogError("MeshStreamHandle: Vertex count mismatch at LOD {} of submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        outVertices.resize(vertexCount);
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            // Position
            outVertices[v].position.x = endian::readLE<float>(file);
            outVertices[v].position.y = endian::readLE<float>(file);
            outVertices[v].position.z = endian::readLE<float>(file);
            // Normal
            outVertices[v].normal.x = endian::readLE<float>(file);
            outVertices[v].normal.y = endian::readLE<float>(file);
            outVertices[v].normal.z = endian::readLE<float>(file);
            // TexCoords
            outVertices[v].texCoords.x = endian::readLE<float>(file);
            outVertices[v].texCoords.y = endian::readLE<float>(file);

            // Bone data for v0.0.7+ files
            if (has64ByteVertices)
            {
                outVertices[v].boneIndices.x = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.y = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.z = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.w = endian::readLE<int32_t>(file);
                outVertices[v].boneWeights.x = endian::readLE<float>(file);
                outVertices[v].boneWeights.y = endian::readLE<float>(file);
                outVertices[v].boneWeights.z = endian::readLE<float>(file);
                outVertices[v].boneWeights.w = endian::readLE<float>(file);
            }
            else
            {
                // Default bone data for older file versions
                outVertices[v].boneIndices = glm::ivec4(-1, -1, -1, -1);
                outVertices[v].boneWeights = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }

            if (file.fail())
            {
                vfLogError("MeshStreamHandle: Failed to read vertex {} of LOD {} submesh {}",
                           v, lodLevel, submeshIdx);
                return false;
            }
        }

        uint32_t indexCount = endian::readLE<uint32_t>(file);
        if (indexCount != lodInfo.indexCount)
        {
            vfLogError("MeshStreamHandle: Index count mismatch at LOD {} of submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        endian::readVectorLE<uint32_t>(file, outIndices, indexCount);

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read indices of LOD {} submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        return true;
    }

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

    bool MeshStreamHandle::readClusterDAG(uint32_t submeshIdx, ClusterDAGData& outData)
    {
        std::lock_guard<std::mutex> lock(fileMutex);

        outData.clear();

        if (!file.is_open())
        {
            vfLogError("MeshStreamHandle: File not open");
            return false;
        }

        if (!hasClusterDAGs)
        {
            return true; // No cluster DAG data is valid (older file)
        }

        if (submeshIdx >= header.numSubmeshes)
        {
            vfLogError("MeshStreamHandle: Invalid submesh index {} (max {})",
                       submeshIdx, header.numSubmeshes);
            return false;
        }

        const auto& submeshInfo = header.submeshes[submeshIdx];
        if (!submeshInfo.hasClusterDAGData)
        {
            return true; // No DAG for this submesh is valid
        }

        file.seekg(submeshInfo.clusterDAGDataOffset);
        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to seek to cluster DAG for submesh {}", submeshIdx);
            return false;
        }

        // Read hasDAG flag
        uint8_t hasDAG = endian::readLE<uint8_t>(file);
        if (hasDAG == 0)
        {
            return true;
        }

        // Read header
        outData.header.clusterCount = endian::readLE<uint32_t>(file);
        outData.header.leafClusterCount = endian::readLE<uint32_t>(file);
        outData.header.maxDepth = endian::readLE<uint32_t>(file);
        outData.header.maxGeometricError = endian::readLE<float>(file);

        outData.header.boundingSphere.x = endian::readLE<float>(file);
        outData.header.boundingSphere.y = endian::readLE<float>(file);
        outData.header.boundingSphere.z = endian::readLE<float>(file);
        outData.header.boundingSphere.w = endian::readLE<float>(file);

        // Read clusters
        outData.clusters.resize(outData.header.clusterCount);

        for (uint32_t i = 0; i < outData.header.clusterCount; ++i)
        {
            auto& cluster = outData.clusters[i];

            // ClusterDescriptor (16 bytes)
            cluster.descriptor.meshletOffset = endian::readLE<uint32_t>(file);
            cluster.descriptor.meshletCount = endian::readLE<uint16_t>(file);
            cluster.descriptor.triangleCount = endian::readLE<uint16_t>(file);
            cluster.descriptor.vertexOffset = endian::readLE<uint32_t>(file);
            cluster.descriptor.vertexCount = endian::readLE<uint32_t>(file);

            // ClusterBounds (32 bytes)
            cluster.bounds.boundingSphere.x = endian::readLE<float>(file);
            cluster.bounds.boundingSphere.y = endian::readLE<float>(file);
            cluster.bounds.boundingSphere.z = endian::readLE<float>(file);
            cluster.bounds.boundingSphere.w = endian::readLE<float>(file);
            cluster.bounds.cone.x = endian::readLE<float>(file);
            cluster.bounds.cone.y = endian::readLE<float>(file);
            cluster.bounds.cone.z = endian::readLE<float>(file);
            cluster.bounds.cone.w = endian::readLE<float>(file);

            // ClusterHierarchy (16 bytes)
            cluster.hierarchy.parentIndex = endian::readLE<uint32_t>(file);
            cluster.hierarchy.siblingIndex = endian::readLE<uint32_t>(file);
            cluster.hierarchy.geometricError = endian::readLE<float>(file);
            cluster.hierarchy.level = endian::readLE<uint16_t>(file);
            cluster.hierarchy.flags = endian::readLE<uint16_t>(file);
        }

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read cluster DAG data for submesh {}", submeshIdx);
            return false;
        }

        vfLogInfo("MeshStreamHandle: Loaded cluster DAG with {} clusters for submesh {}",
                  outData.header.clusterCount, submeshIdx);
        return true;
    }

    std::unique_ptr<MeshStreamHandle> MeshStreamResource::openStream(std::string_view path)
    {
        auto handle = std::make_unique<MeshStreamHandle>();
        if (!handle->openStream(path))
        {
            return nullptr;
        }
        return handle;
    }

    MeshesData MeshStreamResource::loadAll(std::string_view path)
    {
        MeshesData result;

        auto stream = openStream(path);
        if (!stream)
        {
            return result;
        }

        const auto& header = stream->getHeader();
        result.headerFileType = header.headerFileType;
        result.version = header.version;
        result.numberOfMeshes = header.numSubmeshes;
        result.meshes.resize(header.numSubmeshes);

        for (uint32_t i = 0; i < header.numSubmeshes; ++i)
        {
            auto& meshData = result.meshes[i];
            meshData.name = header.submeshes[i].name;
            meshData.lodLevels.resize(LOD_LEVEL_COUNT);

            for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
            {
                if (!stream->readLODLevel(i, lod,
                                          meshData.lodLevels[lod].vertices,
                                          meshData.lodLevels[lod].indices))
                {
                    vfLogError("MeshStreamResource: Failed to read LOD {} of submesh {} from {}",
                               lod, i, path);
                    return MeshesData{};
                }
            }
        }

        // Read skeleton data (v0.0.7+)
        if (stream->hasSkeletonData())
        {
            if (!stream->readSkeleton(result.skeleton))
            {
                vfLogError("MeshStreamResource: Failed to read skeleton from {}", path);
                return MeshesData{};
            }
        }

        return result;
    }

    bool MeshStreamResource::readLODFromFile(std::string_view path,
                                             const LODFileInfo& lodInfo,
                                             std::vector<Vertex>& outVertices,
                                             std::vector<uint32_t>& outIndices,
                                             bool hasBoneData)
    {
        std::ifstream file(std::string(path), std::ios::binary);
        if (!file)
        {
            vfLogError("MeshStreamResource: Failed to open file for LOD read: {}", path);
            return false;
        }

        file.seekg(lodInfo.fileOffset);
        if (file.fail())
        {
            vfLogError("MeshStreamResource: Failed to seek to LOD offset in {}", path);
            return false;
        }

        uint32_t vertexCount = endian::readLE<uint32_t>(file);
        if (vertexCount != lodInfo.vertexCount)
        {
            vfLogError("MeshStreamResource: Vertex count mismatch: expected {}, got {} in {}",
                       lodInfo.vertexCount, vertexCount, path);
            return false;
        }

        outVertices.resize(vertexCount);
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            // Position
            outVertices[v].position.x = endian::readLE<float>(file);
            outVertices[v].position.y = endian::readLE<float>(file);
            outVertices[v].position.z = endian::readLE<float>(file);
            // Normal
            outVertices[v].normal.x = endian::readLE<float>(file);
            outVertices[v].normal.y = endian::readLE<float>(file);
            outVertices[v].normal.z = endian::readLE<float>(file);
            // TexCoords
            outVertices[v].texCoords.x = endian::readLE<float>(file);
            outVertices[v].texCoords.y = endian::readLE<float>(file);

            // Bone data (v0.0.6+)
            if (hasBoneData)
            {
                outVertices[v].boneIndices.x = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.y = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.z = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.w = endian::readLE<int32_t>(file);
                outVertices[v].boneWeights.x = endian::readLE<float>(file);
                outVertices[v].boneWeights.y = endian::readLE<float>(file);
                outVertices[v].boneWeights.z = endian::readLE<float>(file);
                outVertices[v].boneWeights.w = endian::readLE<float>(file);
            }
            else
            {
                // Initialize with defaults for older file versions
                outVertices[v].boneIndices = glm::ivec4(-1, -1, -1, -1);
                outVertices[v].boneWeights = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }

            if (file.fail())
            {
                vfLogError("MeshStreamResource: Failed to read vertex {} in {}", v, path);
                return false;
            }
        }

        uint32_t indexCount = endian::readLE<uint32_t>(file);
        if (indexCount != lodInfo.indexCount)
        {
            vfLogError("MeshStreamResource: Index count mismatch: expected {}, got {} in {}",
                       lodInfo.indexCount, indexCount, path);
            return false;
        }

        endian::readVectorLE<uint32_t>(file, outIndices, indexCount);

        if (file.fail())
        {
            vfLogError("MeshStreamResource: Failed to read indices in {}", path);
            return false;
        }

        return true;
    }
}
