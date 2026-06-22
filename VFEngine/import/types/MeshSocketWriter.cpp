#include "print/Log.hpp"
#include "MeshSocketWriter.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/MeshStreamHandle.hpp"

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

        std::streampos socketOffset = findSocketOffset(meshPath);
        if (socketOffset == std::streampos(0))
            return false;

        std::vector<char> prefixData;
        if (!readFilePrefix(meshPath, socketOffset, prefixData))
            return false;

        if (!writeSocketFile(meshPath, prefixData, sockets))
            return false;

        vfLogDebug("MeshSocketWriter: Saved {} sockets to {}", sockets.size(), meshPath);
        return true;
    }

    std::streampos MeshSocketWriter::findSocketOffset(const std::string& meshPath)
    {
        auto stream = resource::MeshStreamResource::openStream(meshPath);
        if (!stream)
        {
            vfLogError("MeshSocketWriter: Cannot open stream: {}", meshPath);
            return std::streampos(0);
        }

        // VK-1427: static meshes carry sockets too. getSocketDataOffset() is recorded
        // right after the skeleton (or the hasSkinning byte for a static mesh), so no
        // skeleton is required. The prefix [0, offset) is preserved and a fresh SOK2
        // block is appended.
        std::streampos offset = stream->getSocketDataOffset();
        if (offset == std::streampos(0))
        {
            vfLogError("MeshSocketWriter: Failed to get socket data offset");
        }

        return offset;
    }

    bool MeshSocketWriter::readFilePrefix(const std::string& meshPath, std::streampos offset,
                                           std::vector<char>& outData)
    {
        std::ifstream file(meshPath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("MeshSocketWriter: Cannot open file for reading: {}", meshPath);
            return false;
        }

        outData.resize(static_cast<size_t>(offset));
        file.read(outData.data(), static_cast<std::streamsize>(offset));

        if (file.fail())
        {
            vfLogError("MeshSocketWriter: Failed to read file prefix");
            return false;
        }

        return true;
    }

    bool MeshSocketWriter::writeSocketFile(const std::string& meshPath, const std::vector<char>& prefixData,
                                            const std::vector<animator::SocketDefinition>& sockets)
    {
        std::ofstream file(meshPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            vfLogError("MeshSocketWriter: Cannot open file for writing: {}", meshPath);
            return false;
        }

        file.write(prefixData.data(), static_cast<std::streamsize>(prefixData.size()));

        // Versioned socket block (VK-1402) — must mirror MeshSerializer::writeSocketData and
        // MeshStreamHandle::readSocketDefinitions: magic + version precede the count, and
        // version 2 appends a per-socket localRotation (w,x,y,z) after the position.
        constexpr uint32_t kSocketBlockMagic = 0x534F4B32;   // 'SOK2'
        constexpr uint32_t kSocketBlockVersion = 2;          // 2 = adds localRotation
        resource::endian::writeLE<uint32_t>(file, kSocketBlockMagic);
        resource::endian::writeLE<uint32_t>(file, kSocketBlockVersion);

        uint32_t socketCount = static_cast<uint32_t>(sockets.size());
        resource::endian::writeLE<uint32_t>(file, socketCount);

        for (const auto& socket : sockets)
        {
            uint32_t nameLength = static_cast<uint32_t>(socket.name.length());
            resource::endian::writeLE<uint32_t>(file, nameLength);
            if (nameLength > 0)
            {
                file.write(socket.name.data(), nameLength);
            }

            uint32_t boneNameLength = static_cast<uint32_t>(socket.targetBoneName.length());
            resource::endian::writeLE<uint32_t>(file, boneNameLength);
            if (boneNameLength > 0)
            {
                file.write(socket.targetBoneName.data(), boneNameLength);
            }

            resource::endian::writeLE<float>(file, socket.localPosition.x);
            resource::endian::writeLE<float>(file, socket.localPosition.y);
            resource::endian::writeLE<float>(file, socket.localPosition.z);

            resource::endian::writeLE<float>(file, socket.localRotation.w);
            resource::endian::writeLE<float>(file, socket.localRotation.x);
            resource::endian::writeLE<float>(file, socket.localRotation.y);
            resource::endian::writeLE<float>(file, socket.localRotation.z);
        }

        if (file.fail())
        {
            vfLogError("MeshSocketWriter: Failed to write socket data");
            return false;
        }

        return true;
    }
}
