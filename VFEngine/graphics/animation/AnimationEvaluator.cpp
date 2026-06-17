#include "AnimationEvaluator.hpp"
#include "RetargetContext.hpp"
#include "print/Log.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace animation
{
    void AnimationEvaluator::loadAnimation(const resource::AnimationData& animation,
                                           const resource::SkeletonData& skeleton,
                                           const RetargetContext* retargetContext)
    {
        clear();

        animationData = &animation;
        skeletonData = &skeleton;
        // Only honor a non-empty context; an empty/zero-bone context falls back to native.
        retarget = (retargetContext && !retargetContext->empty() &&
                    retargetContext->perTargetBone.size() == skeleton.bones.size())
                       ? retargetContext
                       : nullptr;

        if (skeletonData->bones.empty() || skeletonData->inverseBindPoses.empty())
        {
            vfLogError("Animation '{}' - skeleton has no bones or inverse bind poses",
                        animationData->name);
            return;
        }

        const size_t boneCount = skeletonData->bones.size();

        buildBoneToChannelMap();
        evaluatedBones.resize(boneCount);
        computedLocalBindPoses.resize(boneCount);

        const size_t channelCount = animationData->channels.size();
        positionKeyHints.assign(channelCount, 0);
        rotationKeyHints.assign(channelCount, 0);
        scalingKeyHints.assign(channelCount, 0);

        for (size_t i = 0; i < boneCount; ++i)
        {
            computedLocalBindPoses[i] = skeletonData->bones[i].offsetMatrix;
        }
    }

    void AnimationEvaluator::clear()
    {
        animationData = nullptr;
        skeletonData = nullptr;
        retarget = nullptr;
        boneNameToChannelIndex.clear();
        evaluatedBones.clear();
        computedLocalBindPoses.clear();
        positionKeyHints.clear();
        rotationKeyHints.clear();
        scalingKeyHints.clear();
    }

    void AnimationEvaluator::buildBoneToChannelMap()
    {
        boneNameToChannelIndex.clear();

        if (!animationData) return;

        for (size_t i = 0; i < animationData->channels.size(); ++i)
        {
            boneNameToChannelIndex[animationData->channels[i].boneName] = i;
        }
    }

    void AnimationEvaluator::sampleRetargetedLocal(size_t i, float timeInTicks,
                                                   glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale) const
    {
        outScale = glm::vec3(1.0f); // retargeting ignores source scale; target keeps its proportions

        const RetargetBone& rb = retarget->perTargetBone[i];
        if (rb.mapped)
        {
            auto it = boneNameToChannelIndex.find(rb.sourceBoneName);
            if (it != boneNameToChannelIndex.end())
            {
                const auto& ch = animationData->channels[it->second];

                outRot = ch.rotationKeys.empty()
                             ? rb.targetRefRotation
                             : glm::normalize(rb.correction * interpolateRotation(ch, timeInTicks, it->second));

                if (rb.isRoot)
                {
                    glm::vec3 srcPos = ch.positionKeys.empty()
                                           ? rb.srcHipBindLocal
                                           : interpolatePosition(ch, timeInTicks, it->second);
                    outPos = rb.tgtHipBindLocal + retarget->legLengthRatio * (srcPos - rb.srcHipBindLocal);
                }
                else
                {
                    outPos = glm::vec3(computedLocalBindPoses[i][3]); // keep target bone length
                }
                return;
            }
        }

        // Unmapped target bone, or source bone absent from this clip -> hold target bind pose.
        outPos = glm::vec3(computedLocalBindPoses[i][3]);
        outRot = glm::quat_cast(glm::mat3(computedLocalBindPoses[i]));
    }

    float AnimationEvaluator::secondsToTicks(float seconds) const
    {
        if (!animationData) return 0.0f;
        float tps = animationData->ticksPerSecond > 0.0f ? animationData->ticksPerSecond : 24.0f;
        return seconds * tps;
    }

    std::vector<glm::mat4> AnimationEvaluator::evaluatePose(float timeInTicks) const
    {
        if (!animationData || !skeletonData || skeletonData->bones.empty() || skeletonData->inverseBindPoses.empty())
            return {};

        const size_t boneCount = skeletonData->bones.size();

        if (animationData->duration > 0.0f)
            timeInTicks = std::fmod(timeInTicks, animationData->duration);

        for (size_t i = 0; i < boneCount; ++i)
        {
            const auto& bone = skeletonData->bones[i];

            glm::mat4 animatedTransform;

            if (retarget)
            {
                glm::vec3 rPos; glm::quat rRot; glm::vec3 rScl;
                sampleRetargetedLocal(i, timeInTicks, rPos, rRot, rScl);
                evaluatedBones[i].position = rPos;
                evaluatedBones[i].rotation = rRot;
                evaluatedBones[i].scale = rScl;
                animatedTransform = glm::translate(glm::mat4(1.0f), rPos) * glm::mat4_cast(rRot) *
                                    glm::scale(glm::mat4(1.0f), rScl);
                evaluatedBones[i].localTransform = bone.preTransform * animatedTransform;
                continue;
            }

            auto it = boneNameToChannelIndex.find(bone.name);

            if (it != boneNameToChannelIndex.end())
            {
                const auto& ch = animationData->channels[it->second];

                glm::vec3 pos = ch.positionKeys.empty()
                                    ? glm::vec3(computedLocalBindPoses[i][3])
                                    : interpolatePosition(ch, timeInTicks, it->second);

                glm::quat rot = ch.rotationKeys.empty()
                                    ? glm::quat_cast(glm::mat3(computedLocalBindPoses[i]))
                                    : interpolateRotation(ch, timeInTicks, it->second);

                glm::vec3 scl = ch.scalingKeys.empty()
                                    ? glm::vec3(1.0f)
                                    : interpolateScale(ch, timeInTicks, it->second);

                evaluatedBones[i].position = pos;
                evaluatedBones[i].rotation = rot;
                evaluatedBones[i].scale = scl;
                animatedTransform = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(
                    glm::mat4(1.0f), scl);
            }
            else
            {
                evaluatedBones[i].position = glm::vec3(computedLocalBindPoses[i][3]);
                evaluatedBones[i].rotation = glm::quat_cast(glm::mat3(computedLocalBindPoses[i]));
                evaluatedBones[i].scale = glm::vec3(1.0f);
                animatedTransform = computedLocalBindPoses[i];
            }

            evaluatedBones[i].localTransform = bone.preTransform * animatedTransform;
        }

        for (size_t i = 0; i < boneCount; ++i)
        {
            int parent = skeletonData->bones[i].parentIndex;
            if (parent >= 0)
                evaluatedBones[i].worldTransform =
                    evaluatedBones[parent].worldTransform * evaluatedBones[i].localTransform;
            else
                evaluatedBones[i].worldTransform = evaluatedBones[i].localTransform;

            evaluatedBones[i].skinnedPosition = glm::vec3(evaluatedBones[i].worldTransform[3]);
        }

        const glm::mat4& globalInv = skeletonData->globalInverseTransform;
        std::vector<glm::mat4> result(boneCount);
        for (size_t i = 0; i < boneCount; ++i)
        {
            result[i] = globalInv * evaluatedBones[i].worldTransform * skeletonData->inverseBindPoses[i];
            evaluatedBones[i].skinnedPosition =
                glm::vec3(globalInv * glm::vec4(evaluatedBones[i].skinnedPosition, 1.0f));
        }

        return result;
    }

    std::vector<glm::mat4> AnimationEvaluator::evaluatePose(float timeInTicks, glm::vec3& outRootPosition) const
    {
        if (!animationData || !skeletonData || skeletonData->bones.empty() || skeletonData->inverseBindPoses.empty())
        {
            outRootPosition = glm::vec3(0.0f);
            return {};
        }

        const size_t boneCount = skeletonData->bones.size();

        if (animationData->duration > 0.0f)
            timeInTicks = std::fmod(timeInTicks, animationData->duration);

        // Find the root motion bone: the shallowest bone with animated position keys.
        // Walk from the root down the hierarchy to find the first bone that actually
        // translates (has > 1 position key). Handles Armature → Root → Hips chains.
        int rootMotionBone = -1;
        {
            int bestDepth = std::numeric_limits<int>::max();
            for (size_t b = 0; b < boneCount; ++b)
            {
                auto itB = boneNameToChannelIndex.find(skeletonData->bones[b].name);
                if (itB == boneNameToChannelIndex.end() ||
                    animationData->channels[itB->second].positionKeys.size() <= 1)
                    continue;

                int depth = 0;
                int idx = static_cast<int>(b);
                while (idx >= 0)
                {
                    idx = skeletonData->bones[idx].parentIndex;
                    ++depth;
                }

                if (depth < bestDepth)
                {
                    bestDepth = depth;
                    rootMotionBone = static_cast<int>(b);
                }
            }
        }

        outRootPosition = glm::vec3(0.0f);

        for (size_t i = 0; i < boneCount; ++i)
        {
            const auto& bone = skeletonData->bones[i];

            glm::mat4 animatedTransform;

            if (retarget)
            {
                glm::vec3 rPos; glm::quat rRot; glm::vec3 rScl;
                sampleRetargetedLocal(i, timeInTicks, rPos, rRot, rScl);

                const RetargetBone& rb = retarget->perTargetBone[i];
                if (rb.isRoot)
                {
                    outRootPosition = rPos - rb.tgtHipBindLocal;
                    rPos = rb.tgtHipBindLocal;
                }

                evaluatedBones[i].position = rPos;
                evaluatedBones[i].rotation = rRot;
                evaluatedBones[i].scale = rScl;
                animatedTransform = glm::translate(glm::mat4(1.0f), rPos) * glm::mat4_cast(rRot) *
                                    glm::scale(glm::mat4(1.0f), rScl);
                evaluatedBones[i].localTransform = bone.preTransform * animatedTransform;
                continue;
            }

            auto it = boneNameToChannelIndex.find(bone.name);

            if (it != boneNameToChannelIndex.end())
            {
                const auto& ch = animationData->channels[it->second];

                glm::vec3 pos = ch.positionKeys.empty()
                                    ? glm::vec3(computedLocalBindPoses[i][3])
                                    : interpolatePosition(ch, timeInTicks, it->second);

                glm::quat rot = ch.rotationKeys.empty()
                                    ? glm::quat_cast(glm::mat3(computedLocalBindPoses[i]))
                                    : interpolateRotation(ch, timeInTicks, it->second);

                glm::vec3 scl = ch.scalingKeys.empty()
                                    ? glm::vec3(1.0f)
                                    : interpolateScale(ch, timeInTicks, it->second);

                evaluatedBones[i].position = pos;
                evaluatedBones[i].rotation = rot;
                evaluatedBones[i].scale = scl;

                if (static_cast<int>(i) == rootMotionBone)
                {
                    glm::vec3 bindPos = glm::vec3(computedLocalBindPoses[i][3]);
                    outRootPosition = pos - bindPos;
                    pos = bindPos;
                }

                animatedTransform = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(
                    glm::mat4(1.0f), scl);
            }
            else
            {
                evaluatedBones[i].position = glm::vec3(computedLocalBindPoses[i][3]);
                evaluatedBones[i].rotation = glm::quat_cast(glm::mat3(computedLocalBindPoses[i]));
                evaluatedBones[i].scale = glm::vec3(1.0f);
                animatedTransform = computedLocalBindPoses[i];
            }

            evaluatedBones[i].localTransform = bone.preTransform * animatedTransform;
        }

        for (size_t i = 0; i < boneCount; ++i)
        {
            int parent = skeletonData->bones[i].parentIndex;
            if (parent >= 0)
                evaluatedBones[i].worldTransform =
                    evaluatedBones[parent].worldTransform * evaluatedBones[i].localTransform;
            else
                evaluatedBones[i].worldTransform = evaluatedBones[i].localTransform;

            evaluatedBones[i].skinnedPosition = glm::vec3(evaluatedBones[i].worldTransform[3]);
        }

        const glm::mat4& globalInv = skeletonData->globalInverseTransform;
        std::vector<glm::mat4> result(boneCount);
        for (size_t i = 0; i < boneCount; ++i)
        {
            result[i] = globalInv * evaluatedBones[i].worldTransform * skeletonData->inverseBindPoses[i];
            evaluatedBones[i].skinnedPosition =
                glm::vec3(globalInv * glm::vec4(evaluatedBones[i].skinnedPosition, 1.0f));
        }

        return result;
    }

    template <typename KeyType>
    size_t AnimationEvaluator::findKeyframeIndex(const std::vector<KeyType>& keys, float time, size_t& hint) const
    {
        const size_t lastIndex = keys.size() - 1;

        if (hint < lastIndex && keys[hint].time <= time && time < keys[hint + 1].time)
            return hint;

        size_t next = hint + 1;
        if (next < lastIndex && keys[next].time <= time && time < keys[next + 1].time)
        {
            hint = next;
            return next;
        }

        auto it = std::upper_bound(keys.begin(), keys.end(), time,
            [](float t, const KeyType& key) { return t < key.time; });

        size_t index0;
        if (it == keys.begin())
            index0 = 0;
        else if (it == keys.end())
            index0 = lastIndex;
        else
            index0 = static_cast<size_t>(std::distance(keys.begin(), it)) - 1;

        hint = index0;
        return index0;
    }

    glm::vec3 AnimationEvaluator::interpolatePosition(const resource::BoneAnimation& channel, float time, size_t channelIndex) const
    {
        if (channel.positionKeys.empty())
            return glm::vec3(0.0f);

        if (channel.positionKeys.size() == 1)
            return channel.positionKeys[0].position;

        size_t index0 = findKeyframeIndex(channel.positionKeys, time, positionKeyHints[channelIndex]);
        size_t index1 = (index0 + 1) % channel.positionKeys.size();

        float t0 = channel.positionKeys[index0].time;
        float t1 = channel.positionKeys[index1].time;

        float factor = (t1 != t0) ? glm::clamp((time - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;

        return glm::mix(channel.positionKeys[index0].position, channel.positionKeys[index1].position, factor);
    }

    glm::quat AnimationEvaluator::interpolateRotation(const resource::BoneAnimation& channel, float time, size_t channelIndex) const
    {
        if (channel.rotationKeys.empty())
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        if (channel.rotationKeys.size() == 1)
            return channel.rotationKeys[0].rotation;

        size_t index0 = findKeyframeIndex(channel.rotationKeys, time, rotationKeyHints[channelIndex]);
        size_t index1 = (index0 + 1) % channel.rotationKeys.size();

        float t0 = channel.rotationKeys[index0].time;
        float t1 = channel.rotationKeys[index1].time;

        float factor = (t1 != t0) ? glm::clamp((time - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;

        return glm::slerp(channel.rotationKeys[index0].rotation, channel.rotationKeys[index1].rotation, factor);
    }

    glm::vec3 AnimationEvaluator::interpolateScale(const resource::BoneAnimation& channel, float time, size_t channelIndex) const
    {
        if (channel.scalingKeys.empty())
            return glm::vec3(1.0f);

        if (channel.scalingKeys.size() == 1)
            return channel.scalingKeys[0].scale;

        size_t index0 = findKeyframeIndex(channel.scalingKeys, time, scalingKeyHints[channelIndex]);
        size_t index1 = (index0 + 1) % channel.scalingKeys.size();

        float t0 = channel.scalingKeys[index0].time;
        float t1 = channel.scalingKeys[index1].time;

        float factor = (t1 != t0) ? glm::clamp((time - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;

        return glm::mix(channel.scalingKeys[index0].scale, channel.scalingKeys[index1].scale, factor);
    }

    std::vector<glm::mat4> AnimationEvaluator::evaluatePoseLOD(float timeInTicks, const BoneLODSet& activeBones) const
    {
        if (!animationData || !skeletonData || skeletonData->bones.empty() || skeletonData->inverseBindPoses.empty())
            return {};

        const size_t boneCount = skeletonData->bones.size();

        if (animationData->duration > 0.0f)
            timeInTicks = std::fmod(timeInTicks, animationData->duration);

        for (size_t i = 0; i < boneCount; ++i)
        {
            const auto& bone = skeletonData->bones[i];

            if (i < MAX_SKELETON_BONES && activeBones.test(i))
            {
                glm::mat4 animatedTransform;

                if (retarget)
                {
                    glm::vec3 rPos; glm::quat rRot; glm::vec3 rScl;
                    sampleRetargetedLocal(i, timeInTicks, rPos, rRot, rScl);
                    evaluatedBones[i].position = rPos;
                    evaluatedBones[i].rotation = rRot;
                    evaluatedBones[i].scale = rScl;
                    animatedTransform = glm::translate(glm::mat4(1.0f), rPos) * glm::mat4_cast(rRot) *
                                        glm::scale(glm::mat4(1.0f), rScl);
                    evaluatedBones[i].localTransform = bone.preTransform * animatedTransform;
                    continue;
                }

                auto it = boneNameToChannelIndex.find(bone.name);

                if (it != boneNameToChannelIndex.end())
                {
                    const auto& ch = animationData->channels[it->second];

                    glm::vec3 pos = ch.positionKeys.empty()
                                        ? glm::vec3(computedLocalBindPoses[i][3])
                                        : interpolatePosition(ch, timeInTicks, it->second);

                    glm::quat rot = ch.rotationKeys.empty()
                                        ? glm::quat_cast(glm::mat3(computedLocalBindPoses[i]))
                                        : interpolateRotation(ch, timeInTicks, it->second);

                    glm::vec3 scl = ch.scalingKeys.empty()
                                        ? glm::vec3(1.0f)
                                        : interpolateScale(ch, timeInTicks, it->second);

                    evaluatedBones[i].position = pos;
                    evaluatedBones[i].rotation = rot;
                    evaluatedBones[i].scale = scl;
                    animatedTransform = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(
                        glm::mat4(1.0f), scl);
                }
                else
                {
                    evaluatedBones[i].position = glm::vec3(computedLocalBindPoses[i][3]);
                    evaluatedBones[i].rotation = glm::quat_cast(glm::mat3(computedLocalBindPoses[i]));
                    evaluatedBones[i].scale = glm::vec3(1.0f);
                    animatedTransform = computedLocalBindPoses[i];
                }

                evaluatedBones[i].localTransform = bone.preTransform * animatedTransform;
            }
            else
            {
                evaluatedBones[i].localTransform = bone.preTransform * computedLocalBindPoses[i];
            }
        }

        for (size_t i = 0; i < boneCount; ++i)
        {
            int parent = skeletonData->bones[i].parentIndex;
            if (parent >= 0)
                evaluatedBones[i].worldTransform =
                    evaluatedBones[parent].worldTransform * evaluatedBones[i].localTransform;
            else
                evaluatedBones[i].worldTransform = evaluatedBones[i].localTransform;
        }

        const glm::mat4& globalInv = skeletonData->globalInverseTransform;
        std::vector<glm::mat4> result(boneCount);
        for (size_t i = 0; i < boneCount; ++i)
        {
            result[i] = globalInv * evaluatedBones[i].worldTransform * skeletonData->inverseBindPoses[i];
        }

        return result;
    }
}
