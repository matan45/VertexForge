#include "AnimationResource.hpp"
#include "SkeletonResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <filesystem>

namespace resource
{
    AnimationData AnimationResource::loadAnimation(std::string_view path)
    {
        std::shared_ptr<SkeletonData> skeleton;
        return loadAnimationWithSkeleton(path, skeleton);
    }

    AnimationData AnimationResource::loadAnimationWithSkeleton(std::string_view path,
                                                               std::shared_ptr<SkeletonData>& skeletonOut)
    {
        AnimationData data;
        skeletonOut = nullptr;

        std::ifstream file(path.data(), std::ios::binary);
        if (!file)
        {
            vfLogError("Failed to open animation file: {}", path);
            return data;
        }

        // Read and validate header
        uint8_t fileType = endian::readLE<uint8_t>(file);
        if (static_cast<FileType>(fileType) != FileType::ANIMATION)
        {
            vfLogError("Invalid animation file type: expected {}, got {}",
                       static_cast<int>(FileType::ANIMATION), static_cast<int>(fileType));
            return data;
        }

        // Read version
        data.version.major = endian::readLE<uint32_t>(file);
        data.version.minor = endian::readLE<uint32_t>(file);
        data.version.patch = endian::readLE<uint32_t>(file);

        // Read animation name
        data.name = readString(file);

        // Read metadata
        data.duration = endian::readLE<float>(file);
        data.ticksPerSecond = endian::readLE<float>(file);

        // Check version for format type
        // v0.0.6+: self-contained with skeleton + inverse bind poses
        // v0.0.5: has skeleton reference AND inline skeleton
        // v0.0.4: has skeleton reference only (loads from .vfSkeleton)
        // v0.0.3 and earlier: inline skeleton only (no reference)
        bool hasSkeletonReference = (data.version.major == 0 && data.version.minor == 0 && data.version.patch >= 4);
        bool hasInlineSkeleton = (data.version.major == 0 && data.version.minor == 0 && data.version.patch >= 5) ||
                                  (data.version.major == 0 && data.version.minor == 0 && data.version.patch < 4);
        bool hasInverseBindPoses = (data.version.major == 0 && data.version.minor == 0 && data.version.patch >= 6);
        bool hasMeshData = (data.version.major == 0 && data.version.minor == 0 && data.version.patch >= 7);

        if (hasSkeletonReference)
        {
            // Read skeleton reference (present in v0.0.4+)
            data.skeletonReference = readString(file);
            vfLogInfo("Animation has skeleton reference: {}", data.skeletonReference);
        }

        if (hasInlineSkeleton)
        {
            // Read inline skeleton (v0.0.5+ or legacy v0.0.3 and earlier)
            uint32_t numBones = endian::readLE<uint32_t>(file);
            if (numBones > 1000)  // Sanity check
            {
                vfLogError("Invalid bone count in animation file: {}", numBones);
                return data;
            }

            data.skeleton.resize(numBones);
            for (auto& bone : data.skeleton)
            {
                bone.name = readString(file);
                bone.parentIndex = endian::readLE<int32_t>(file);

                // Read offset matrix (16 floats, column-major)
                bone.offsetMatrix = readMatrix(file);

                // Read preTransform matrix (16 floats, column-major)
                bone.preTransform = readMatrix(file);
            }
            vfLogInfo("Loaded inline skeleton with {} bones", numBones);
        }
        else if (hasSkeletonReference && !data.skeletonReference.empty())
        {
            // v0.0.4 format: load skeleton from referenced file
            std::filesystem::path animPath(path);
            std::filesystem::path skelPath = animPath.parent_path() / data.skeletonReference;

            if (std::filesystem::exists(skelPath))
            {
                skeletonOut = SkeletonResource::loadSkeletonCached(skelPath.string());
                if (skeletonOut && skeletonOut->hasBones())
                {
                    // Populate skeleton field from loaded skeleton
                    data.skeleton = skeletonOut->bones;
                    data.globalInverseTransform = skeletonOut->globalInverseTransform;
                    // Also copy inverse bind poses from skeleton if available
                    data.inverseBindPoses = skeletonOut->inverseBindPoses;
                    vfLogInfo("Loaded skeleton from reference: {}", data.skeletonReference);
                }
                else
                {
                    vfLogWarning("Failed to load skeleton from reference: {}", skelPath.string());
                }
            }
            else
            {
                vfLogWarning("Skeleton file not found: {}", skelPath.string());
            }
        }

        // Read inverse bind poses (v0.0.6+)
        if (hasInverseBindPoses)
        {
            uint32_t numPoses = endian::readLE<uint32_t>(file);
            if (numPoses > 1000)  // Sanity check
            {
                vfLogError("Invalid inverse bind pose count in animation file: {}", numPoses);
                return data;
            }

            data.inverseBindPoses.resize(numPoses);
            for (auto& matrix : data.inverseBindPoses)
            {
                matrix = readMatrix(file);
            }
            vfLogInfo("Loaded {} inverse bind poses", numPoses);
        }

        // Read channels
        uint32_t numChannels = endian::readLE<uint32_t>(file);
        if (numChannels > 1000)  // Sanity check
        {
            vfLogError("Invalid channel count in animation file: {}", numChannels);
            return data;
        }

        data.channels.resize(numChannels);
        for (auto& channel : data.channels)
        {
            channel.boneName = readString(file);

            // Read position keys
            uint32_t numPosKeys = endian::readLE<uint32_t>(file);
            channel.positionKeys.resize(numPosKeys);
            for (auto& key : channel.positionKeys)
            {
                key.time = endian::readLE<float>(file);
                key.position.x = endian::readLE<float>(file);
                key.position.y = endian::readLE<float>(file);
                key.position.z = endian::readLE<float>(file);
            }

            // Read rotation keys
            uint32_t numRotKeys = endian::readLE<uint32_t>(file);
            channel.rotationKeys.resize(numRotKeys);
            for (auto& key : channel.rotationKeys)
            {
                key.time = endian::readLE<float>(file);
                key.rotation.x = endian::readLE<float>(file);
                key.rotation.y = endian::readLE<float>(file);
                key.rotation.z = endian::readLE<float>(file);
                key.rotation.w = endian::readLE<float>(file);
            }

            // Read scaling keys
            uint32_t numScaleKeys = endian::readLE<uint32_t>(file);
            channel.scalingKeys.resize(numScaleKeys);
            for (auto& key : channel.scalingKeys)
            {
                key.time = endian::readLE<float>(file);
                key.scale.x = endian::readLE<float>(file);
                key.scale.y = endian::readLE<float>(file);
                key.scale.z = endian::readLE<float>(file);
            }
        }

        // Read global inverse transform
        if (file.peek() != EOF)
        {
            data.globalInverseTransform = readMatrix(file);
        }
        else
        {
            data.globalInverseTransform = glm::mat4(1.0f);
        }

        // Read mesh data (v0.0.7+)
        if (hasMeshData && file.peek() != EOF)
        {
            uint32_t numVertices = endian::readLE<uint32_t>(file);
            if (numVertices > 0 && numVertices < 10000000)
            {
                data.vertices.resize(numVertices);
                for (auto& v : data.vertices)
                {
                    v.position.x = endian::readLE<float>(file);
                    v.position.y = endian::readLE<float>(file);
                    v.position.z = endian::readLE<float>(file);
                    v.normal.x = endian::readLE<float>(file);
                    v.normal.y = endian::readLE<float>(file);
                    v.normal.z = endian::readLE<float>(file);
                    v.texCoords.x = endian::readLE<float>(file);
                    v.texCoords.y = endian::readLE<float>(file);
                    v.boneIndices.x = endian::readLE<int32_t>(file);
                    v.boneIndices.y = endian::readLE<int32_t>(file);
                    v.boneIndices.z = endian::readLE<int32_t>(file);
                    v.boneIndices.w = endian::readLE<int32_t>(file);
                    v.boneWeights.x = endian::readLE<float>(file);
                    v.boneWeights.y = endian::readLE<float>(file);
                    v.boneWeights.z = endian::readLE<float>(file);
                    v.boneWeights.w = endian::readLE<float>(file);
                }

                uint32_t numIndices = endian::readLE<uint32_t>(file);
                if (numIndices > 0 && numIndices < 100000000)
                {
                    data.indices.resize(numIndices);
                    for (auto& idx : data.indices)
                        idx = endian::readLE<uint32_t>(file);
                }
                vfLogInfo("Loaded mesh: {} vertices, {} indices", data.vertices.size(), data.indices.size());
            }
        }

        data.headerFileType = FileType::ANIMATION;

        vfLogInfo("Loaded animation '{}' - {} bones, {} channels, {} vertices, duration: {:.2f}s",
                  data.name, data.skeleton.size(), data.channels.size(), data.vertices.size(),
                  data.duration / data.ticksPerSecond);

        return data;
    }

    bool AnimationResource::validateFile(std::string_view path)
    {
        std::ifstream file(path.data(), std::ios::binary);
        if (!file)
        {
            return false;
        }

        uint8_t fileType = endian::readLE<uint8_t>(file);
        return static_cast<FileType>(fileType) == FileType::ANIMATION;
    }

    std::string AnimationResource::readString(std::ifstream& file)
    {
        uint32_t length = endian::readLE<uint32_t>(file);
        if (length == 0)
        {
            return "";
        }

        if (length > 10000)  // Sanity check for string length
        {
            return "";
        }

        std::string str(length, '\0');
        file.read(str.data(), length);
        return str;
    }

    glm::mat4 AnimationResource::readMatrix(std::ifstream& file)
    {
        glm::mat4 matrix(1.0f);
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                matrix[col][row] = endian::readLE<float>(file);
            }
        }
        return matrix;
    }
}
