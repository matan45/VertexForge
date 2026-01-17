#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "resource/Types.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace animation
{
    struct EvaluatedBone
    {
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 localTransform{1.0f};
        glm::mat4 worldTransform{1.0f};
        glm::vec3 skinnedPosition{0.0f};
    };

    class AnimationEvaluator
    {
    private:
        const resource::AnimationData* animationData = nullptr;
        const resource::SkeletonData* skeletonData = nullptr;
        std::unordered_map<std::string, size_t> boneNameToChannelIndex;
        mutable std::vector<EvaluatedBone> evaluatedBones;
        std::vector<glm::mat4> computedBindPoses;
        std::vector<glm::mat4> computedLocalBindPoses;

    public:
        AnimationEvaluator() = default;
        ~AnimationEvaluator() = default;

        void loadAnimation(const resource::AnimationData& animation, const resource::SkeletonData& skeleton);
        void clear();

        std::vector<glm::mat4> evaluatePose(float timeInTicks) const;

        const std::vector<EvaluatedBone>& getEvaluatedBones() const { return evaluatedBones; }

        bool isLoaded() const { return animationData != nullptr && skeletonData != nullptr && !skeletonData->bones.empty(); }

        float secondsToTicks(float seconds) const;

    private:
        glm::vec3 interpolatePosition(const resource::BoneAnimation& channel, float time) const;
        glm::quat interpolateRotation(const resource::BoneAnimation& channel, float time) const;
        glm::vec3 interpolateScale(const resource::BoneAnimation& channel, float time) const;

        void buildBoneToChannelMap();
    };
}
