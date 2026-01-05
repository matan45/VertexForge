#include "MeshResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>

namespace resource
{
    // Helper function to read a single LOD level from file
    static bool readLODLevel(std::ifstream& inFile, LODLevel& lodLevel, uint32_t meshIdx, uint32_t lodIdx,
                             uint32_t maxVertexCount, uint32_t maxIndexCount)
    {
        // Read vertex count
        uint32_t vertexCount = endian::readLE<uint32_t>(inFile);

        if (vertexCount > maxVertexCount)
        {
            vfLogError("Vertex count {} exceeds maximum limit {} in mesh {} LOD {}",
                       vertexCount, maxVertexCount, meshIdx, lodIdx);
            return false;
        }

        // Read vertices
        lodLevel.vertices.resize(vertexCount);
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            lodLevel.vertices[v].position.x = endian::readLE<float>(inFile);
            lodLevel.vertices[v].position.y = endian::readLE<float>(inFile);
            lodLevel.vertices[v].position.z = endian::readLE<float>(inFile);
            lodLevel.vertices[v].normal.x = endian::readLE<float>(inFile);
            lodLevel.vertices[v].normal.y = endian::readLE<float>(inFile);
            lodLevel.vertices[v].normal.z = endian::readLE<float>(inFile);
            lodLevel.vertices[v].texCoords.x = endian::readLE<float>(inFile);
            lodLevel.vertices[v].texCoords.y = endian::readLE<float>(inFile);

            if (inFile.fail())
            {
                vfLogError("Failed to read vertex {} of mesh {} LOD {}", v, meshIdx, lodIdx);
                return false;
            }
        }

        // Read index count
        uint32_t indexCount = endian::readLE<uint32_t>(inFile);

        if (indexCount > maxIndexCount)
        {
            vfLogError("Index count {} exceeds maximum limit {} in mesh {} LOD {}",
                       indexCount, maxIndexCount, meshIdx, lodIdx);
            return false;
        }

        // Read indices
        endian::readVectorLE<uint32_t>(inFile, lodLevel.indices, indexCount);

        if (inFile.fail())
        {
            vfLogError("Failed to read indices of mesh {} LOD {}", meshIdx, lodIdx);
            return false;
        }

        return true;
    }

    MeshesData MeshResource::loadMesh(std::string_view path)
    {
        MeshesData result;

        std::string filePath(path);
        std::ifstream inFile(filePath, std::ios::binary);
        if (!inFile)
        {
            vfLogError("Failed to open mesh file: {}", path);
            return result;
        }

        // Read header
        uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
        result.headerFileType = static_cast<FileType>(headerFileType);

        uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

        result.version.major = majorVersion;
        result.version.minor = minorVersion;
        result.version.patch = patchVersion;

        // Determine format version
        bool isLODFormat = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 3);
        bool hasMeshlets = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 4);

        if (!isLODFormat)
        {
            vfLogError("Incompatible mesh file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
            return result;
        }

        result.numberOfMeshes = endian::readLE<uint32_t>(inFile);
        result.meshes.resize(result.numberOfMeshes);

        if (inFile.fail())
        {
            vfLogError("Failed to read mesh header: {}", path);
            return result;
        }
        
        for (uint32_t meshIdx = 0; meshIdx < result.numberOfMeshes; ++meshIdx)
        {
            auto& meshData = result.meshes[meshIdx];

            // Read submesh name
            uint32_t nameLength = endian::readLE<uint32_t>(inFile);
            if (nameLength > 0 && nameLength < 1024)
            {
                meshData.name.resize(nameLength);
                inFile.read(meshData.name.data(), nameLength);
            }
            else if (nameLength == 0)
            {
                meshData.name = "SubMesh_" + std::to_string(meshIdx);
            }
            
            
            uint32_t lodLevelCount = endian::readLE<uint32_t>(inFile);

            if (lodLevelCount == 0 || lodLevelCount > 8)
            {
                vfLogError("Invalid LOD level count {} in mesh {}", lodLevelCount, meshIdx);
                return result;
            }

            meshData.lodLevels.resize(lodLevelCount);

            for (uint32_t lodIdx = 0; lodIdx < lodLevelCount; ++lodIdx)
            {
                if (!readLODLevel(inFile, meshData.lodLevels[lodIdx], meshIdx, lodIdx,
                                  maxVertexCount, maxIndexCount))
                {
                    return result;
                }
            }

            // Skip meshlet data if present (v0.0.4+)
            if (hasMeshlets)
            {
                // Read per-LOD meshlet headers to calculate skip size
                uint32_t totalMeshlets = 0;
                uint32_t totalVertexIndices = 0;
                uint32_t totalPrimitives = 0;

                for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
                {
                    uint32_t meshletCount = endian::readLE<uint32_t>(inFile);
                    uint32_t vertexCount = endian::readLE<uint32_t>(inFile);
                    uint32_t primitiveCount = endian::readLE<uint32_t>(inFile);
                    totalMeshlets += meshletCount;
                    totalVertexIndices += vertexCount;
                    totalPrimitives += primitiveCount;
                }

                // Skip meshlet descriptors + bounds (44 bytes each)
                // Skip vertex indices (4 bytes each)
                // Skip primitive data (4 bytes each)
                size_t skipBytes = totalMeshlets * 44 + totalVertexIndices * 4 + totalPrimitives * 4;
                inFile.seekg(skipBytes, std::ios::cur);

                if (inFile.fail())
                {
                    vfLogError("Failed to skip meshlet data for mesh {}", meshIdx);
                    return result;
                }
            }
        }

        vfLogInfo("Loaded mesh file with {} meshes (LOD format v{}.{}.{}): {}",
                  result.numberOfMeshes, majorVersion, minorVersion, patchVersion, path);

        return result;
    }

    // Helper function to read meshlet data for a submesh
    static bool readMeshletData(std::ifstream& inFile, SubmeshMeshletData& meshletData,
                                uint32_t meshIdx, uint32_t maxMeshletCount)
    {
        // Read per-LOD meshlet info header
        struct LODMeshletHeader {
            uint32_t meshletCount;
            uint32_t vertexCount;
            uint32_t primitiveCount;
        };
        std::array<LODMeshletHeader, LOD_LEVEL_COUNT> lodHeaders{};

        uint32_t totalMeshlets = 0;
        uint32_t totalVertices = 0;
        uint32_t totalPrimitives = 0;

        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod) {
            lodHeaders[lod].meshletCount = endian::readLE<uint32_t>(inFile);
            lodHeaders[lod].vertexCount = endian::readLE<uint32_t>(inFile);
            lodHeaders[lod].primitiveCount = endian::readLE<uint32_t>(inFile);

            if (lodHeaders[lod].meshletCount > maxMeshletCount) {
                vfLogError("Meshlet count {} exceeds maximum {} in mesh {} LOD {}",
                           lodHeaders[lod].meshletCount, maxMeshletCount, meshIdx, lod);
                return false;
            }

            totalMeshlets += lodHeaders[lod].meshletCount;
            totalVertices += lodHeaders[lod].vertexCount;
            totalPrimitives += lodHeaders[lod].primitiveCount;
        }

        if (inFile.fail()) {
            vfLogError("Failed to read meshlet headers for mesh {}", meshIdx);
            return false;
        }

        // Setup LOD info offsets
        uint32_t meshletOffset = 0;
        uint32_t vertexOffset = 0;
        uint32_t primitiveOffset = 0;

        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod) {
            meshletData.lodLevels[lod].meshletOffset = meshletOffset;
            meshletData.lodLevels[lod].meshletCount = lodHeaders[lod].meshletCount;
            meshletData.lodLevels[lod].vertexDataOffset = vertexOffset;
            meshletData.lodLevels[lod].vertexDataCount = lodHeaders[lod].vertexCount;
            meshletData.lodLevels[lod].primitiveDataOffset = primitiveOffset;
            meshletData.lodLevels[lod].primitiveDataCount = lodHeaders[lod].primitiveCount;

            meshletOffset += lodHeaders[lod].meshletCount;
            vertexOffset += lodHeaders[lod].vertexCount;
            primitiveOffset += lodHeaders[lod].primitiveCount;
        }

        // Read all meshlet descriptors and bounds
        meshletData.meshlets.resize(totalMeshlets);
        for (uint32_t i = 0; i < totalMeshlets; ++i) {
            auto& meshlet = meshletData.meshlets[i];

            // Read descriptor (12 bytes)
            meshlet.descriptor.vertexOffset = endian::readLE<uint32_t>(inFile);
            meshlet.descriptor.primitiveOffset = endian::readLE<uint32_t>(inFile);
            meshlet.descriptor.vertexCount = endian::readLE<uint8_t>(inFile);
            meshlet.descriptor.primitiveCount = endian::readLE<uint8_t>(inFile);
            meshlet.descriptor.padding = endian::readLE<uint16_t>(inFile);

            // Read bounds (32 bytes)
            meshlet.bounds.boundingSphere.x = endian::readLE<float>(inFile);
            meshlet.bounds.boundingSphere.y = endian::readLE<float>(inFile);
            meshlet.bounds.boundingSphere.z = endian::readLE<float>(inFile);
            meshlet.bounds.boundingSphere.w = endian::readLE<float>(inFile);
            meshlet.bounds.cone.x = endian::readLE<float>(inFile);
            meshlet.bounds.cone.y = endian::readLE<float>(inFile);
            meshlet.bounds.cone.z = endian::readLE<float>(inFile);
            meshlet.bounds.cone.w = endian::readLE<float>(inFile);
        }

        if (inFile.fail()) {
            vfLogError("Failed to read meshlet descriptors for mesh {}", meshIdx);
            return false;
        }

        // Read all meshlet vertex indices
        meshletData.meshletVertices.resize(totalVertices);
        for (uint32_t i = 0; i < totalVertices; ++i) {
            meshletData.meshletVertices[i] = endian::readLE<uint32_t>(inFile);
        }

        if (inFile.fail()) {
            vfLogError("Failed to read meshlet vertex indices for mesh {}", meshIdx);
            return false;
        }

        // Read all meshlet primitive data (packed)
        meshletData.meshletPrimitives.resize(totalPrimitives);
        for (uint32_t i = 0; i < totalPrimitives; ++i) {
            meshletData.meshletPrimitives[i] = endian::readLE<uint32_t>(inFile);
        }

        if (inFile.fail()) {
            vfLogError("Failed to read meshlet primitives for mesh {}", meshIdx);
            return false;
        }

        return true;
    }

    MeshesDataWithMeshlets MeshResource::loadMeshWithMeshlets(std::string_view path)
    {
        MeshesDataWithMeshlets result;

        std::string filePath(path);
        std::ifstream inFile(filePath, std::ios::binary);
        if (!inFile)
        {
            vfLogError("Failed to open mesh file: {}", path);
            return result;
        }

        // Read header
        uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
        result.headerFileType = static_cast<FileType>(headerFileType);

        uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

        result.version.major = majorVersion;
        result.version.minor = minorVersion;
        result.version.patch = patchVersion;

        // Check for meshlet format (v0.0.4+)
        bool hasMeshlets = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 4);

        if (!hasMeshlets)
        {
            vfLogError("Mesh file does not contain meshlet data (requires v0.0.4+): {}.{}.{}",
                       majorVersion, minorVersion, patchVersion);
            return result;
        }

        result.numberOfMeshes = endian::readLE<uint32_t>(inFile);
        result.meshes.resize(result.numberOfMeshes);

        if (inFile.fail())
        {
            vfLogError("Failed to read mesh header: {}", path);
            return result;
        }

        for (uint32_t meshIdx = 0; meshIdx < result.numberOfMeshes; ++meshIdx)
        {
            auto& meshData = result.meshes[meshIdx];

            // Read submesh name
            uint32_t nameLength = endian::readLE<uint32_t>(inFile);
            if (nameLength > 0 && nameLength < 1024)
            {
                meshData.name.resize(nameLength);
                inFile.read(meshData.name.data(), nameLength);
            }
            else if (nameLength == 0)
            {
                meshData.name = "SubMesh_" + std::to_string(meshIdx);
            }

            // Copy name to meshlet data as well
            meshData.meshletData.name = meshData.name;

            uint32_t lodLevelCount = endian::readLE<uint32_t>(inFile);

            if (lodLevelCount == 0 || lodLevelCount > 8)
            {
                vfLogError("Invalid LOD level count {} in mesh {}", lodLevelCount, meshIdx);
                return result;
            }

            meshData.lodLevels.resize(lodLevelCount);

            // Read vertex/index data for each LOD
            for (uint32_t lodIdx = 0; lodIdx < lodLevelCount; ++lodIdx)
            {
                if (!readLODLevel(inFile, meshData.lodLevels[lodIdx], meshIdx, lodIdx,
                                  maxVertexCount, maxIndexCount))
                {
                    return result;
                }
            }

            // Read meshlet data
            if (!readMeshletData(inFile, meshData.meshletData, meshIdx, maxMeshletCount))
            {
                return result;
            }
        }

        vfLogInfo("Loaded mesh file with {} meshes and meshlets (v{}.{}.{}): {}",
                  result.numberOfMeshes, majorVersion, minorVersion, patchVersion, path);

        return result;
    }
}
