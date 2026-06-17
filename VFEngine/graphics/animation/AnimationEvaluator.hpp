#pragma once

#include "AnimationExport.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "resource/Types.hpp"
#include "BoneLOD.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace animation
{
    struct RetargetContext;

    struct EvaluatedBone
    {
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 localTransform{1.0f};
        glm::mat4 worldTransform{1.0f};
        glm::vec3 skinnedPosition{0.0f};
    };

#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_ANIMATION_API AnimationEvaluator
    {
    private:
        const resource::AnimationData* animationData = nullptr;
        const resource::SkeletonData* skeletonData = nullptr;
        // When set, animationData is the SOURCE clip and skeletonData is the TARGET
        // skeleton; per-bone sampling is remapped source -> target (VK-910).
        // Null = native (no retargeting), behaves byte-for-byte as before.
        const RetargetContext* retarget = nullptr;
        std::unordered_map<std::string, size_t> boneNameToChannelIndex;
        // Per target bone, the source channel index resolved once at loadAnimation
        // (-1 = unmapped or source bone absent from this clip). Only used when retarget != null.
        std::vector<int> retargetChannelIndex;
        mutable std::vector<EvaluatedBone> evaluatedBones;
        std::vector<glm::mat4> computedLocalBindPoses;

        mutable std::vector<size_t> positionKeyHints;
        mutable std::vector<size_t> rotationKeyHints;
        mutable std::vector<size_t> scalingKeyHints;

    public:
        AnimationEvaluator() = default;
        ~AnimationEvaluator() = default;

        void loadAnimation(const resource::AnimationData& animation, const resource::SkeletonData& skeleton,
                           const RetargetContext* retargetContext = nullptr);
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

        // Retarget sampling for target bone i -> local TRS (used only when retarget != null).
        void sampleRetargetedLocal(size_t i, float timeInTicks,
                                   glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale) const;
    };
#pragma warning(pop)
}
