#include "SkeletonResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>

namespace resource
{
    // Static cache initialization
    std::unordered_map<std::string, std::shared_ptr<SkeletonData>> SkeletonResource::skeletonCache;

    SkeletonData SkeletonResource::loadSkeleton(std::string_view path)
    {
        SkeletonData data;

        std::ifstream file(path.data(), std::ios::binary);
        if (!file)
        {
            vfLogError("Failed to open skeleton file: {}", path);
            return data;
        }

        // Read and validate magic header "VFSK"
        char magic[4];
        file.read(magic, 4);
        if (magic[0] != 'V' || magic[1] != 'F' || magic[2] != 'S' || magic[3] != 'K')
        {
            vfLogError("Invalid skeleton file magic: expected VFSK");
            return data;
        }

        // Read version
        data.version.major = endian::readLE<uint32_t>(file);
        data.version.minor = endian::readLE<uint32_t>(file);
        data.version.patch = endian::readLE<uint32_t>(file);

        // Read skeleton name
        data.name = readString(file);

        // Read bone count
        uint32_t numBones = endian::readLE<uint32_t>(file);
        if (numBones > 1000)  // Sanity check
        {
            vfLogError("Invalid bone count in skeleton file: {}", numBones);
            return data;
        }

        data.bones.resize(numBones);
        data.bindPoses.resize(numBones);
        data.inverseBindPoses.resize(numBones);

        for (size_t i = 0; i < numBones; ++i)
        {
            auto& bone = data.bones[i];

            // Read bone name
            bone.name = readString(file);

            // Read parent index
            bone.parentIndex = endian::readLE<int32_t>(file);

            // Read offset matrix (local transform)
            bone.offsetMatrix = readMatrix(file);

            // Read pre-transform
            bone.preTransform = readMatrix(file);

            // Read bind pose (world space)
            data.bindPoses[i] = readMatrix(file);

            // Read inverse bind pose
            data.inverseBindPoses[i] = readMatrix(file);
        }

        // Read global inverse transform
        data.globalInverseTransform = readMatrix(file);

        data.headerFileType = FileType::SKELETON;

        vfLogInfo("Loaded skeleton '{}' - {} bones", data.name, data.bones.size());

        return data;
    }

    std::shared_ptr<SkeletonData> SkeletonResource::loadSkeletonCached(std::string_view path)
    {
        std::string pathStr(path);

        // Check cache first
        auto it = skeletonCache.find(pathStr);
        if (it != skeletonCache.end())
        {
            vfLogInfo("Using cached skeleton: {}", path);
            return it->second;
        }

        // Load and cache
        auto skeleton = std::make_shared<SkeletonData>(loadSkeleton(path));
        if (skeleton->hasBones())
        {
            skeletonCache[pathStr] = skeleton;
        }

        return skeleton;
    }

    void SkeletonResource::clearCache()
    {
        skeletonCache.clear();
        vfLogInfo("Skeleton cache cleared");
    }

    bool SkeletonResource::validateFile(std::string_view path)
    {
        std::ifstream file(path.data(), std::ios::binary);
        if (!file)
        {
            return false;
        }

        // Check magic header "VFSK"
        char magic[4];
        file.read(magic, 4);
        return magic[0] == 'V' && magic[1] == 'F' && magic[2] == 'S' && magic[3] == 'K';
    }

    std::string SkeletonResource::readString(std::ifstream& file)
    {
        uint32_t length = endian::readLE<uint32_t>(file);
        if (length == 0)
        {
            return "";
        }

        if (length > 10000)  // Sanity check
        {
            return "";
        }

        std::string str(length, '\0');
        file.read(str.data(), length);
        return str;
    }

    glm::mat4 SkeletonResource::readMatrix(std::ifstream& file)
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
