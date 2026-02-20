#include "MeshSocketWriter.hpp"
#include "resource/EndianUtils.hpp"
#include "print/EditorLogger.hpp"

#include <fstream>
#include <vector>
#include <filesystem>

namespace types
{
    std::streampos MeshSocketWriter::findSocketDataOffset(const std::string& meshPath)
    {
        std::ifstream file(meshPath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("MeshSocketWriter: Cannot open file: {}", meshPath);
            return 0;
        }

        // Parse header
        uint8_t fileType = resource::endian::readLE<uint8_t>(file);
        uint32_t majorVersion = resource::endian::readLE<uint32_t>(file);
        uint32_t minorVersion = resource::endian::readLE<uint32_t>(file);
        uint32_t patchVersion = resource::endian::readLE<uint32_t>(file);

        bool isV007 = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 7);
        if (!isV007)
        {
            vfLogError("MeshSocketWriter: File version {}.{}.{} does not support skeleton/sockets",
                       majorVersion, minorVersion, patchVersion);
            return 0;
        }

        bool hasMeshlets = (patchVersion >= 4);
        bool hasConvexHulls = (patchVersion >= 5);

        uint32_t numSubmeshes = resource::endian::readLE<uint32_t>(file);

        // Skip skeleton reference string
        uint32_t skeletonRefLength = resource::endian::readLE<uint32_t>(file);
        if (skeletonRefLength > 0)
        {
            file.seekg(skeletonRefLength, std::ios::cur);
        }

        // Skip all submesh data (LODs, meshlets, convex)
        for (uint32_t meshIdx = 0; meshIdx < numSubmeshes; ++meshIdx)
        {
            // Submesh name
            uint32_t nameLength = resource::endian::readLE<uint32_t>(file);
            if (nameLength > 0 && nameLength < 1024)
            {
                file.seekg(nameLength, std::ios::cur);
            }

            // LOD levels
            uint32_t lodLevelCount = resource::endian::readLE<uint32_t>(file);
            for (uint32_t lodIdx = 0; lodIdx < lodLevelCount; ++lodIdx)
            {
                uint32_t vertexCount = resource::endian::readLE<uint32_t>(file);
                file.seekg(static_cast<std::streamoff>(vertexCount) * 64, std::ios::cur); // 64-byte vertices (v0.0.7+)

                uint32_t indexCount = resource::endian::readLE<uint32_t>(file);
                file.seekg(static_cast<std::streamoff>(indexCount) * sizeof(uint32_t), std::ios::cur);
            }

            // Meshlet data
            if (hasMeshlets)
            {
                uint32_t totalMeshlets = 0;
                uint32_t totalVertexIndices = 0;
                uint32_t totalPrimitives = 0;

                // LOD_LEVEL_COUNT = 4 (from resource::LOD_LEVEL_COUNT)
                for (uint32_t lod = 0; lod < 4; ++lod)
                {
                    uint32_t meshletCount = resource::endian::readLE<uint32_t>(file);
                    uint32_t vertexIndexCount = resource::endian::readLE<uint32_t>(file);
                    uint32_t primitiveCount = resource::endian::readLE<uint32_t>(file);
                    totalMeshlets += meshletCount;
                    totalVertexIndices += vertexIndexCount;
                    totalPrimitives += primitiveCount;
                }

                // Skip meshlet descriptors (44 bytes each: 12 descriptor + 32 bounds) + vertex indices + primitive indices
                file.seekg(static_cast<std::streamoff>(totalMeshlets) * 44, std::ios::cur);
                file.seekg(static_cast<std::streamoff>(totalVertexIndices) * sizeof(uint32_t), std::ios::cur);
                file.seekg(static_cast<std::streamoff>(totalPrimitives) * sizeof(uint32_t), std::ios::cur);
            }

            // Convex hull data
            if (hasConvexHulls)
            {
                uint8_t hasDecomp = resource::endian::readLE<uint8_t>(file);
                if (hasDecomp != 0)
                {
                    file.seekg(20, std::ios::cur); // Skip parameters

                    uint32_t numHulls = resource::endian::readLE<uint32_t>(file);
                    for (uint32_t h = 0; h < numHulls; ++h)
                    {
                        uint32_t vertexCount = resource::endian::readLE<uint32_t>(file);
                        file.seekg(static_cast<std::streamoff>(vertexCount) * 3 * sizeof(float), std::ios::cur);

                        uint32_t indexCount = resource::endian::readLE<uint32_t>(file);
                        file.seekg(static_cast<std::streamoff>(indexCount) * sizeof(uint32_t), std::ios::cur);
                        file.seekg(4 * sizeof(float), std::ios::cur); // center + volume
                    }
                }
            }

            if (file.fail())
            {
                vfLogError("MeshSocketWriter: Failed parsing submesh {}", meshIdx);
                return 0;
            }
        }

        // Now at skeleton section
        uint8_t hasSkinning = resource::endian::readLE<uint8_t>(file);
        if (hasSkinning == 0)
        {
            vfLogError("MeshSocketWriter: Mesh has no skeleton data");
            return 0;
        }

        uint32_t boneCount = resource::endian::readLE<uint32_t>(file);
        if (boneCount > 1000)
        {
            vfLogError("MeshSocketWriter: Invalid bone count {}", boneCount);
            return 0;
        }

        // Skip bone data
        for (uint32_t b = 0; b < boneCount; ++b)
        {
            uint32_t nameLength = resource::endian::readLE<uint32_t>(file);
            if (nameLength > 1024)
            {
                vfLogError("MeshSocketWriter: Invalid bone name length");
                return 0;
            }
            file.seekg(nameLength, std::ios::cur); // name
            file.seekg(4, std::ios::cur);           // parentIndex
            file.seekg(16 * 4, std::ios::cur);      // offsetMatrix
            file.seekg(16 * 4, std::ios::cur);      // preTransform
        }

        // Skip inverse bind poses
        file.seekg(static_cast<std::streamoff>(boneCount) * 16 * sizeof(float), std::ios::cur);

        // Skip global inverse transform
        file.seekg(16 * sizeof(float), std::ios::cur);

        if (file.fail())
        {
            vfLogError("MeshSocketWriter: Failed to parse skeleton data");
            return 0;
        }

        // This is where socket data starts (or should start)
        return file.tellg();
    }

    bool MeshSocketWriter::saveSocketsToMesh(const std::string& meshPath,
                                              const std::vector<animator::SocketDefinition>& sockets)
    {
        if (!std::filesystem::exists(meshPath))
        {
            vfLogError("MeshSocketWriter: File does not exist: {}", meshPath);
            return false;
        }

        // Find where socket data should be written
        std::streampos socketOffset = findSocketDataOffset(meshPath);
        if (socketOffset == std::streampos(0))
        {
            return false;
        }

        // Read all file data before the socket section
        std::vector<char> prefixData;
        {
            std::ifstream file(meshPath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("MeshSocketWriter: Cannot open file for reading: {}", meshPath);
                return false;
            }

            prefixData.resize(static_cast<size_t>(socketOffset));
            file.read(prefixData.data(), static_cast<std::streamsize>(socketOffset));

            if (file.fail())
            {
                vfLogError("MeshSocketWriter: Failed to read file prefix");
                return false;
            }
        }

        // Rewrite the file: prefix + new socket data
        {
            std::ofstream file(meshPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                vfLogError("MeshSocketWriter: Cannot open file for writing: {}", meshPath);
                return false;
            }

            // Write everything before sockets
            file.write(prefixData.data(), static_cast<std::streamsize>(prefixData.size()));

            // Write socket data using the same format as MeshSerializer::writeSocketData
            uint32_t socketCount = static_cast<uint32_t>(sockets.size());
            resource::endian::writeLE<uint32_t>(file, socketCount);

            for (const auto& socket : sockets)
            {
                // Socket name
                uint32_t nameLength = static_cast<uint32_t>(socket.name.length());
                resource::endian::writeLE<uint32_t>(file, nameLength);
                if (nameLength > 0)
                {
                    file.write(socket.name.data(), nameLength);
                }

                // Target bone name
                uint32_t boneNameLength = static_cast<uint32_t>(socket.targetBoneName.length());
                resource::endian::writeLE<uint32_t>(file, boneNameLength);
                if (boneNameLength > 0)
                {
                    file.write(socket.targetBoneName.data(), boneNameLength);
                }

                // Local position
                resource::endian::writeLE<float>(file, socket.localPosition.x);
                resource::endian::writeLE<float>(file, socket.localPosition.y);
                resource::endian::writeLE<float>(file, socket.localPosition.z);
            }

            if (file.fail())
            {
                vfLogError("MeshSocketWriter: Failed to write socket data");
                return false;
            }
        }

        vfLogInfo("MeshSocketWriter: Saved {} sockets to {}", sockets.size(), meshPath);
        return true;
    }
}
