#include "MeshIKChainWriter.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/EditorLogger.hpp"

#include <fstream>
#include <vector>
#include <filesystem>

namespace types
{
    bool MeshIKChainWriter::saveIKChainsToMesh(const std::string& meshPath,
                                                const std::vector<components::IKChainConfig>& chains)
    {
        if (!std::filesystem::exists(meshPath))
        {
            vfLogError("MeshIKChainWriter: File does not exist: {}", meshPath);
            return false;
        }

        std::streampos ikOffset = findIKChainOffset(meshPath);
        if (ikOffset == std::streampos(0))
            return false;

        std::vector<char> prefixData;
        if (!readFilePrefix(meshPath, ikOffset, prefixData))
            return false;

        if (!writeIKChainFile(meshPath, prefixData, chains))
            return false;

        vfLogInfo("MeshIKChainWriter: Saved {} IK chains to {}", chains.size(), meshPath);
        return true;
    }

    std::streampos MeshIKChainWriter::findIKChainOffset(const std::string& meshPath)
    {
        auto stream = resource::MeshStreamResource::openStream(meshPath);
        if (!stream)
        {
            vfLogError("MeshIKChainWriter: Cannot open stream: {}", meshPath);
            return std::streampos(0);
        }

        if (!stream->hasSkeletonData())
        {
            vfLogError("MeshIKChainWriter: Mesh has no skeleton data");
            return std::streampos(0);
        }

        std::streampos offset = stream->getIKChainDataOffset();
        if (offset == std::streampos(0))
        {
            vfLogError("MeshIKChainWriter: Failed to get IK chain data offset");
        }

        return offset;
    }

    bool MeshIKChainWriter::readFilePrefix(const std::string& meshPath, std::streampos offset,
                                            std::vector<char>& outData)
    {
        std::ifstream file(meshPath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("MeshIKChainWriter: Cannot open file for reading: {}", meshPath);
            return false;
        }

        outData.resize(static_cast<size_t>(offset));
        file.read(outData.data(), static_cast<std::streamsize>(offset));

        if (file.fail())
        {
            vfLogError("MeshIKChainWriter: Failed to read file prefix");
            return false;
        }

        return true;
    }

    bool MeshIKChainWriter::writeIKChainFile(const std::string& meshPath, const std::vector<char>& prefixData,
                                              const std::vector<components::IKChainConfig>& chains)
    {
        std::ofstream file(meshPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            vfLogError("MeshIKChainWriter: Cannot open file for writing: {}", meshPath);
            return false;
        }

        // Write everything before IK chain data
        file.write(prefixData.data(), static_cast<std::streamsize>(prefixData.size()));

        // Write IK chain count
        uint32_t chainCount = static_cast<uint32_t>(chains.size());
        resource::endian::writeLE<uint32_t>(file, chainCount);

        for (const auto& chain : chains)
        {
            // Chain name
            uint32_t chainNameLen = static_cast<uint32_t>(chain.chainName.length());
            resource::endian::writeLE<uint32_t>(file, chainNameLen);
            if (chainNameLen > 0)
            {
                file.write(chain.chainName.data(), chainNameLen);
            }

            // Tip bone name
            uint32_t tipBoneNameLen = static_cast<uint32_t>(chain.tipBoneName.length());
            resource::endian::writeLE<uint32_t>(file, tipBoneNameLen);
            if (tipBoneNameLen > 0)
            {
                file.write(chain.tipBoneName.data(), tipBoneNameLen);
            }

            // Chain bone names
            uint32_t boneCount = static_cast<uint32_t>(chain.chainBoneNames.size());
            resource::endian::writeLE<uint32_t>(file, boneCount);
            for (const auto& boneName : chain.chainBoneNames)
            {
                uint32_t boneNameLen = static_cast<uint32_t>(boneName.length());
                resource::endian::writeLE<uint32_t>(file, boneNameLen);
                if (boneNameLen > 0)
                {
                    file.write(boneName.data(), boneNameLen);
                }
            }

            // Constraints
            uint32_t constraintCount = static_cast<uint32_t>(chain.constraints.size());
            resource::endian::writeLE<uint32_t>(file, constraintCount);
            for (const auto& constraint : chain.constraints)
            {
                resource::endian::writeLE<uint8_t>(file, static_cast<uint8_t>(constraint.type));
                resource::endian::writeLE<float>(file, constraint.hingeAxis.x);
                resource::endian::writeLE<float>(file, constraint.hingeAxis.y);
                resource::endian::writeLE<float>(file, constraint.hingeAxis.z);
                resource::endian::writeLE<float>(file, constraint.coneAngle);
                resource::endian::writeLE<float>(file, constraint.swingAngle);
                resource::endian::writeLE<float>(file, constraint.twistMin);
                resource::endian::writeLE<float>(file, constraint.twistMax);
            }

            // Weight and enabled
            resource::endian::writeLE<float>(file, chain.weight);
            resource::endian::writeLE<uint8_t>(file, chain.enabled ? 1 : 0);
        }

        if (file.fail())
        {
            vfLogError("MeshIKChainWriter: Failed to write IK chain data");
            return false;
        }

        return true;
    }
}
