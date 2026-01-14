#pragma once
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "config/Config.hpp"
#include "resource/Types.hpp"

struct aiScene;
struct aiAnimation;
struct aiNode;

namespace types
{
    using AnimationProgressCallback = std::function<void(float progress)>;

    class Animation
    {
    public:
        // Extract and save all animations from a loaded Assimp scene
        void extractFromScene(const aiScene* scene, std::string_view fileName,
                              std::string_view location,
                              AnimationProgressCallback progressCallback = nullptr) const;

        // Load scene from file and extract animations
        void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                          std::string_view location,
                          AnimationProgressCallback progressCallback = nullptr) const;

    private:
        // Extract skeleton hierarchy from scene
        std::vector<resource::SkeletonBone> extractSkeleton(const aiScene* scene) const;

        // Build bone hierarchy by traversing node tree
        void buildBoneHierarchy(const aiNode* node,
                                const std::unordered_set<std::string>& boneNames,
                                std::vector<resource::SkeletonBone>& bones,
                                std::unordered_map<std::string, int32_t>& boneIndexMap,
                                int32_t parentIndex) const;

        // Extract single animation clip data
        resource::AnimationData extractAnimation(const aiAnimation* anim,
                                                 const std::vector<resource::SkeletonBone>& skeleton) const;

        // Save animation clip to .vfAnim file
        void saveToFile(std::string_view location, std::string_view baseName,
                        const resource::AnimationData& animData) const;

        // Write helpers
        void writeString(std::ofstream& file, const std::string& str) const;
        void writeSkeleton(std::ofstream& file, const std::vector<resource::SkeletonBone>& skeleton) const;
        void writeChannels(std::ofstream& file, const std::vector<resource::BoneAnimation>& channels) const;

        // Sanitize animation name for filename
        static std::string sanitizeAnimationName(const std::string& name);
    };
}
