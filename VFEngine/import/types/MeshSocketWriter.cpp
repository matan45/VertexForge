#include "MeshSocketWriter.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/EditorLogger.hpp"

#include <fstream>
#include <vector>
#include <filesystem>

namespace types
{
    bool MeshSocketWriter::saveSocketsToMesh(const std::string& meshPath,
                                              const std::vector<animator::SocketDefinition>& sockets)
    {
        if (!std::filesystem::exists(meshPath))
        {
            vfLogError("MeshSocketWriter: File does not exist: {}", meshPath);
            return false;
        }

        // Use the canonical stream reader to find socket data offset
        auto stream = resource::MeshStreamResource::openStream(meshPath);
        if (!stream)
        {
            vfLogError("MeshSocketWriter: Cannot open stream: {}", meshPath);
            return false;
        }

        if (!stream->hasSkeletonData())
        {
            vfLogError("MeshSocketWriter: Mesh has no skeleton data");
            return false;
        }

        std::streampos socketOffset = stream->getSocketDataOffset();
        if (socketOffset == std::streampos(0))
        {
            vfLogError("MeshSocketWriter: Failed to get socket data offset");
            return false;
        }

        // Close the stream before rewriting the file
        stream.reset();

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
