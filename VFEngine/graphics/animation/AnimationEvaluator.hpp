#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "resource/Types.hpp"
#include "BoneLOD.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <bitset>

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
        std::vector<glm::mat4> computedLocalBindPoses;

        mutable std::vector<size_t> positionKeyHints;
        mutable std::vector<size_t> rotationKeyHints;
        mutable std::vector<size_t> scalingKeyHints;

    public:
        AnimationEvaluator() = default;
        ~AnimationEvaluator() = default;

        void loadAnimation(const resource::AnimationData& animation, const resource::SkeletonData& skeleton);
        void clear();

        std::vector<glm::mat4> evaluatePose(float timeInTicks) const;
        std::vector<glm::mat4> evaluatePose(float timeInTicks, glm::vec3& outRootPosition) const;
        std::vector<glm::mat4> evaluatePoseLOD(float timeInTicks, const BoneLODSet& activeBones) const;

        const std::vector<EvaluatedBone>& getEvaluatedBones() const { return evaluatedBones; }

        bool isLoaded() const { return animationData != nullptr && skeletonData != nullptr && !skeletonData->bones.empty(); }

        float secondsToTicks(float seconds) const;

    private:
        glm::vec3 interpolatePosition(const resource::BoneAnimation& channel, float time, size_t channelIndex) const;
        glm::quat interpolateRotation(const resource::BoneAnimation& channel, float time, size_t channelIndex) const;
        glm::vec3 interpolateScale(const resource::BoneAnimation& channel, float time, size_t channelIndex) const;

        template <typename KeyType>
        size_t findKeyframeIndex(const std::vector<KeyType>& keys, float time, size_t& hint) const;

        void buildBoneToChannelMap();
    };
}
