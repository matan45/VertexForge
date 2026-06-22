#include "print/Log.hpp"
#include "MeshSocketWriter.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/MeshStreamHandle.hpp"

#include <cstdint>
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

        std::streampos suffixOffset = 0;
        std::streampos socketOffset = findSocketOffset(meshPath, suffixOffset);
        if (socketOffset == std::streampos(0))
            return false;

        std::vector<char> prefixData;
        if (!readFilePrefix(meshPath, socketOffset, prefixData))
            return false;

        // Preserve everything after the existing socket block (the IK-chain block lives
        // here for skeletal meshes). Truncating without this silently destroyed IK chains
        // whenever sockets were saved. suffixOffset == 0 means nothing trails the socket
        // block (e.g. static meshes), so the suffix stays empty.
        std::vector<char> suffixData;
        if (suffixOffset != std::streampos(0) && !readFileSuffix(meshPath, suffixOffset, suffixData))
            return false;

        if (!writeSocketFile(meshPath, prefixData, sockets, suffixData))
            return false;

        vfLogDebug("MeshSocketWriter: Saved {} sockets to {} (preserved {} trailing bytes)",
                   sockets.size(), meshPath, suffixData.size());
        return true;
    }

    std::streampos MeshSocketWriter::findSocketOffset(const std::string& meshPath, std::streampos& outSuffixOffset)
    {
        outSuffixOffset = std::streampos(0);

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

        // End of the existing socket block == start of the IK-chain block (skeletal meshes).
        // Returns 0 for static meshes (no skeleton), where the socket block is the last block
        // and there is nothing trailing to preserve.
        outSuffixOffset = stream->getIKChainDataOffset();

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

    bool MeshSocketWriter::readFileSuffix(const std::string& meshPath, std::streampos offset,
                                           std::vector<char>& outData)
    {
        outData.clear();

        std::error_code ec;
        const std::uintmax_t fileSize = std::filesystem::file_size(meshPath, ec);
        if (ec)
        {
            vfLogError("MeshSocketWriter: Failed to query file size: {}", meshPath);
            return false;
        }

        const std::uintmax_t start = static_cast<std::uintmax_t>(offset);
        if (start >= fileSize)
            return true; // nothing trails the socket block

        std::ifstream file(meshPath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("MeshSocketWriter: Cannot open file for reading suffix: {}", meshPath);
            return false;
        }

        const size_t suffixSize = static_cast<size_t>(fileSize - start);
        outData.resize(suffixSize);
        file.seekg(offset);
        file.read(outData.data(), static_cast<std::streamsize>(suffixSize));

        if (file.fail())
        {
            vfLogError("MeshSocketWriter: Failed to read file suffix");
            return false;
        }

        return true;
    }

    bool MeshSocketWriter::writeSocketFile(const std::string& meshPath, const std::vector<char>& prefixData,
                                            const std::vector<animator::SocketDefinition>& sockets,
                                            const std::vector<char>& suffixData)
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

        // Re-append any block that followed the old socket region (IK chains, etc.) so a
        // socket save no longer truncates trailing data.
        if (!suffixData.empty())
        {
            file.write(suffixData.data(), static_cast<std::streamsize>(suffixData.size()));
        }

        if (file.fail())
        {
            vfLogError("MeshSocketWriter: Failed to write socket data");
            return false;
        }

        return true;
    }
}
