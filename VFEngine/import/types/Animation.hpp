#pragma once
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
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
        
        void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                          std::string_view location,
                          AnimationProgressCallback progressCallback = nullptr) const;

    private:
        void extractFromScene(const aiScene* scene, std::string_view fileName,
                             std::string_view location,
                             AnimationProgressCallback progressCallback = nullptr) const;
        
        std::vector<resource::SkeletonBone> extractSkeleton(const aiScene* scene) const;
        std::unordered_map<std::string, glm::mat4> extractInverseBindPoses(const aiScene* scene) const;
        void buildNodeMap(const aiNode* node, std::unordered_map<std::string, const aiNode*>& nodeMap) const;

        resource::AnimationData extractAnimation(const aiAnimation* anim,
                                                 const std::vector<resource::SkeletonBone>& skeleton,
                                                 const std::unordered_map<std::string, glm::mat4>& inverseBindPoseMap,
                                                 const glm::mat4& globalInverseTransform) const;

        void saveToFile(std::string_view location, std::string_view baseName,
                        const resource::AnimationData& animData) const;

        void extractMeshData(const aiScene* scene,
                            const std::unordered_map<std::string, int32_t>& boneIndexMap,
                            std::vector<resource::Vertex>& outVertices,
                            std::vector<uint32_t>& outIndices) const;

        void writeString(std::ofstream& file, const std::string& str) const;
        void writeSkeleton(std::ofstream& file, const std::vector<resource::SkeletonBone>& skeleton) const;
        void writeInverseBindPoses(std::ofstream& file, const std::vector<glm::mat4>& inverseBindPoses) const;
        void writeChannels(std::ofstream& file, const std::vector<resource::BoneAnimation>& channels) const;
        void writeMeshData(std::ofstream& file, const std::vector<resource::Vertex>& vertices,
                          const std::vector<uint32_t>& indices) const;

        static std::string sanitizeAnimationName(const std::string& name);
    };
}
