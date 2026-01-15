#pragma once
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "config/Config.hpp"
#include "resource/Types.hpp"

struct aiScene;
struct aiNode;

namespace types
{
    using SkeletonProgressCallback = std::function<void(float progress)>;

    class Skeleton
    {
    public:
        // Extract skeleton from scene and save to .vfSkeleton file
        // Returns the path to the saved skeleton file, or empty string on failure
        std::string extractAndSave(const aiScene* scene, std::string_view fileName,
                                   std::string_view location,
                                   SkeletonProgressCallback progressCallback = nullptr) const;

        // Extract skeleton data from scene without saving
        resource::SkeletonData extractFromScene(const aiScene* scene) const;

        // Save skeleton data to file
        void saveToFile(std::string_view location, std::string_view baseName,
                        const resource::SkeletonData& skeleton) const;

        // Check if a skeleton file already exists for a given mesh
        static bool skeletonExists(std::string_view location, std::string_view meshFileName);

        // Get the skeleton file path for a given mesh
        static std::string getSkeletonPath(std::string_view location, std::string_view meshFileName);

    private:
        // Collect all bone names from meshes
        std::unordered_set<std::string> collectBoneNames(const aiScene* scene) const;

        // Extract inverse bind poses from mesh bones
        std::unordered_map<std::string, glm::mat4> extractInverseBindPoses(const aiScene* scene) const;

        // Build bone hierarchy by traversing node tree
        void buildBoneHierarchy(const aiNode* node,
                                const std::unordered_set<std::string>& boneNames,
                                std::vector<resource::SkeletonBone>& bones,
                                std::unordered_map<std::string, int32_t>& boneIndexMap,
                                int32_t parentIndex,
                                const glm::mat4& accumulatedTransform) const;

        // Compute world-space bind poses from bone hierarchy
        void computeBindPoses(const std::vector<resource::SkeletonBone>& bones,
                              std::vector<glm::mat4>& bindPoses) const;

        // Write helpers
        void writeString(std::ofstream& file, const std::string& str) const;
        void writeMatrix(std::ofstream& file, const glm::mat4& matrix) const;
    };
}
