#include "AnimationEvaluator.hpp"
#include "print/Logger.hpp"

namespace animation
{
    void AnimationEvaluator::loadAnimation(const resource::AnimationData& animation,
                                           const resource::SkeletonData& skeleton)
    {
        clear();

        animationData = &animation;
        skeletonData = &skeleton;

        if (skeletonData->bones.empty() || skeletonData->inverseBindPoses.empty())
        {
            loggerError("Animation '{}' - skeleton has no bones or inverse bind poses",
                        animationData->name);
            return;
        }

        const size_t boneCount = skeletonData->bones.size();

        buildBoneToChannelMap();
        evaluatedBones.resize(boneCount);
        computedLocalBindPoses.resize(boneCount);

        for (size_t i = 0; i < boneCount; ++i)
        {
            computedLocalBindPoses[i] = skeletonData->bones[i].offsetMatrix;
        }
    }

    void AnimationEvaluator::clear()
    {
        animationData = nullptr;
        skeletonData = nullptr;
        boneNameToChannelIndex.clear();
        evaluatedBones.clear();
        computedLocalBindPoses.clear();
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
            auto it = boneNameToChannelIndex.find(bone.name);

            glm::mat4 animatedTransform;

            if (it != boneNameToChannelIndex.end())
            {
                const auto& ch = animationData->channels[it->second];

                glm::vec3 pos = ch.positionKeys.empty()
                                    ? glm::vec3(computedLocalBindPoses[i][3])
                                    : interpolatePosition(ch, timeInTicks);

                glm::quat rot = ch.rotationKeys.empty()
                                    ? glm::quat_cast(glm::mat3(computedLocalBindPoses[i]))
                                    : interpolateRotation(ch, timeInTicks);

                glm::vec3 scl = ch.scalingKeys.empty()
                                    ? glm::vec3(1.0f)
                                    : interpolateScale(ch, timeInTicks);

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

                // Compute depth in hierarchy
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
            auto it = boneNameToChannelIndex.find(bone.name);

            glm::mat4 animatedTransform;

            if (it != boneNameToChannelIndex.end())
            {
                const auto& ch = animationData->channels[it->second];

                glm::vec3 pos = ch.positionKeys.empty()
                                    ? glm::vec3(computedLocalBindPoses[i][3])
                                    : interpolatePosition(ch, timeInTicks);

                glm::quat rot = ch.rotationKeys.empty()
                                    ? glm::quat_cast(glm::mat3(computedLocalBindPoses[i]))
                                    : interpolateRotation(ch, timeInTicks);

                glm::vec3 scl = ch.scalingKeys.empty()
                                    ? glm::vec3(1.0f)
                                    : interpolateScale(ch, timeInTicks);

                evaluatedBones[i].position = pos;
                evaluatedBones[i].rotation = rot;
                evaluatedBones[i].scale = scl;

                // Extract horizontal root motion (XZ) and zero it; keep Y for correct height
                if (static_cast<int>(i) == rootMotionBone)
                {
                    outRootPosition = glm::vec3(pos.x, 0.0f, pos.z);
                    pos.x = 0.0f;
                    pos.z = 0.0f;
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

    glm::vec3 AnimationEvaluator::interpolatePosition(const resource::BoneAnimation& channel, float time) const
    {
        if (channel.positionKeys.empty())
            return glm::vec3(0.0f);

        if (channel.positionKeys.size() == 1)
            return channel.positionKeys[0].position;

        size_t index0 = 0;
        for (size_t i = 0; i < channel.positionKeys.size() - 1; ++i)
        {
            if (time < channel.positionKeys[i + 1].time)
            {
                index0 = i;
                break;
            }
            index0 = i;
        }

        size_t index1 = (index0 + 1) % channel.positionKeys.size();

        float t0 = channel.positionKeys[index0].time;
        float t1 = channel.positionKeys[index1].time;

        float factor = (t1 != t0) ? glm::clamp((time - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;

        return glm::mix(channel.positionKeys[index0].position, channel.positionKeys[index1].position, factor);
    }

    glm::quat AnimationEvaluator::interpolateRotation(const resource::BoneAnimation& channel, float time) const
    {
        if (channel.rotationKeys.empty())
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        if (channel.rotationKeys.size() == 1)
            return channel.rotationKeys[0].rotation;

        size_t index0 = 0;
        for (size_t i = 0; i < channel.rotationKeys.size() - 1; ++i)
        {
            if (time < channel.rotationKeys[i + 1].time)
            {
                index0 = i;
                break;
            }
            index0 = i;
        }

        size_t index1 = (index0 + 1) % channel.rotationKeys.size();

        float t0 = channel.rotationKeys[index0].time;
        float t1 = channel.rotationKeys[index1].time;

        float factor = (t1 != t0) ? glm::clamp((time - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;

        return glm::slerp(channel.rotationKeys[index0].rotation, channel.rotationKeys[index1].rotation, factor);
    }

    glm::vec3 AnimationEvaluator::interpolateScale(const resource::BoneAnimation& channel, float time) const
    {
        if (channel.scalingKeys.empty())
            return glm::vec3(1.0f);

        if (channel.scalingKeys.size() == 1)
            return channel.scalingKeys[0].scale;

        size_t index0 = 0;
        for (size_t i = 0; i < channel.scalingKeys.size() - 1; ++i)
        {
            if (time < channel.scalingKeys[i + 1].time)
            {
                index0 = i;
                break;
            }
            index0 = i;
        }

        size_t index1 = (index0 + 1) % channel.scalingKeys.size();

        float t0 = channel.scalingKeys[index0].time;
        float t1 = channel.scalingKeys[index1].time;

        float factor = (t1 != t0) ? glm::clamp((time - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;

        return glm::mix(channel.scalingKeys[index0].scale, channel.scalingKeys[index1].scale, factor);
    }
}
