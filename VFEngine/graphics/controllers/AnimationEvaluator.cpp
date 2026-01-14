#include "AnimationEvaluator.hpp"
#include <algorithm>

namespace controllers
{
    void AnimationEvaluator::loadAnimation(const resource::AnimationData& animation,
                                           const resource::SkeletonInfo& skeleton)
    {
        animationData = &animation;
        meshSkeleton = &skeleton;

        buildBoneToChannelMap();
        buildSkeletonMapping();

        // Initialize evaluated bones for animation skeleton
        evaluatedBones.resize(animation.skeleton.size());
    }

    void AnimationEvaluator::clear()
    {
        animationData = nullptr;
        meshSkeleton = nullptr;
        boneNameToChannelIndex.clear();
        meshBoneToAnimBone.clear();
        animBoneToMeshBone.clear();
        evaluatedBones.clear();
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

    void AnimationEvaluator::buildSkeletonMapping()
    {
        meshBoneToAnimBone.clear();
        animBoneToMeshBone.clear();

        if (!animationData || !meshSkeleton) return;

        // Build animation bone name to index map
        std::unordered_map<std::string, size_t> animBoneNameToIndex;
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            animBoneNameToIndex[animationData->skeleton[i].name] = i;
        }

        // Build mesh bone name to index map
        std::unordered_map<std::string, size_t> meshBoneNameToIndex;
        for (size_t i = 0; i < meshSkeleton->boneNames.size(); ++i)
        {
            meshBoneNameToIndex[meshSkeleton->boneNames[i]] = i;
        }

        // Map mesh skeleton bones to animation skeleton bones
        meshBoneToAnimBone.resize(meshSkeleton->boneNames.size(), -1);
        for (size_t i = 0; i < meshSkeleton->boneNames.size(); ++i)
        {
            auto it = animBoneNameToIndex.find(meshSkeleton->boneNames[i]);
            if (it != animBoneNameToIndex.end())
            {
                meshBoneToAnimBone[i] = static_cast<int32_t>(it->second);
            }
        }

        // Map animation skeleton bones to mesh skeleton bones
        animBoneToMeshBone.resize(animationData->skeleton.size(), -1);
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            auto it = meshBoneNameToIndex.find(animationData->skeleton[i].name);
            if (it != meshBoneNameToIndex.end())
            {
                animBoneToMeshBone[i] = static_cast<int32_t>(it->second);
            }
        }
    }

    std::vector<glm::mat4> AnimationEvaluator::evaluatePose(float timeInTicks) const
    {
        if (!animationData || !meshSkeleton)
        {
            return {};
        }

        // Evaluate each bone in the animation skeleton
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const auto& animBone = animationData->skeleton[i];
            EvaluatedBone& eval = evaluatedBones[i];

            // Find animation channel for this bone
            auto it = boneNameToChannelIndex.find(animBone.name);
            if (it != boneNameToChannelIndex.end())
            {
                const auto& channel = animationData->channels[it->second];
                eval.position = interpolatePosition(channel, timeInTicks);
                eval.rotation = interpolateRotation(channel, timeInTicks);
                eval.scale = interpolateScale(channel, timeInTicks);

                // Compute local transform: T * R * S
                glm::mat4 T = glm::translate(glm::mat4(1.0f), eval.position);
                glm::mat4 R = glm::mat4_cast(eval.rotation);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), eval.scale);
                eval.localTransform = T * R * S;
            }
            else
            {
                // No animation channel - use the bone's default offset transform
                eval.localTransform = animBone.offsetMatrix;
                eval.position = glm::vec3(0.0f);
                eval.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                eval.scale = glm::vec3(1.0f);
            }
        }

        // Compute world transforms
        computeWorldTransforms();

        // Build final bone matrices for GPU
        // Result should be indexed by mesh skeleton bone index
        std::vector<glm::mat4> boneMatrices(meshSkeleton->boneNames.size(), glm::mat4(1.0f));

        for (size_t meshBoneIdx = 0; meshBoneIdx < meshSkeleton->boneNames.size(); ++meshBoneIdx)
        {
            int32_t animBoneIdx = meshBoneToAnimBone[meshBoneIdx];
            if (animBoneIdx >= 0 && animBoneIdx < static_cast<int32_t>(evaluatedBones.size()))
            {
                // Final skinning matrix = worldTransform * inverseBindPose
                boneMatrices[meshBoneIdx] =
                    evaluatedBones[animBoneIdx].worldTransform *
                    meshSkeleton->inverseBindPoses[meshBoneIdx];
            }
            else
            {
                // Bone not animated - use identity (no transformation)
                boneMatrices[meshBoneIdx] = glm::mat4(1.0f);
            }
        }

        return boneMatrices;
    }

    void AnimationEvaluator::computeWorldTransforms() const
    {
        if (!animationData) return;

        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const auto& animBone = animationData->skeleton[i];
            EvaluatedBone& eval = evaluatedBones[i];

            if (animBone.parentIndex >= 0 &&
                animBone.parentIndex < static_cast<int32_t>(evaluatedBones.size()))
            {
                eval.worldTransform =
                    evaluatedBones[animBone.parentIndex].worldTransform * eval.localTransform;
            }
            else
            {
                // Root bone
                eval.worldTransform = eval.localTransform;
            }
        }
    }

    glm::vec3 AnimationEvaluator::interpolatePosition(const resource::BoneAnimation& channel,
                                                       float time) const
    {
        if (channel.positionKeys.empty())
            return glm::vec3(0.0f);

        if (channel.positionKeys.size() == 1)
            return channel.positionKeys[0].position;

        // Find the two keyframes surrounding the current time
        size_t i = 0;
        for (; i < channel.positionKeys.size() - 1; ++i)
        {
            if (time < channel.positionKeys[i + 1].time)
                break;
        }

        if (i >= channel.positionKeys.size() - 1)
            return channel.positionKeys.back().position;

        const auto& k0 = channel.positionKeys[i];
        const auto& k1 = channel.positionKeys[i + 1];

        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = glm::clamp(t, 0.0f, 1.0f);

        return glm::mix(k0.position, k1.position, t);
    }

    glm::quat AnimationEvaluator::interpolateRotation(const resource::BoneAnimation& channel,
                                                       float time) const
    {
        if (channel.rotationKeys.empty())
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        if (channel.rotationKeys.size() == 1)
            return channel.rotationKeys[0].rotation;

        size_t i = 0;
        for (; i < channel.rotationKeys.size() - 1; ++i)
        {
            if (time < channel.rotationKeys[i + 1].time)
                break;
        }

        if (i >= channel.rotationKeys.size() - 1)
            return channel.rotationKeys.back().rotation;

        const auto& k0 = channel.rotationKeys[i];
        const auto& k1 = channel.rotationKeys[i + 1];

        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = glm::clamp(t, 0.0f, 1.0f);

        return glm::slerp(k0.rotation, k1.rotation, t);
    }

    glm::vec3 AnimationEvaluator::interpolateScale(const resource::BoneAnimation& channel,
                                                    float time) const
    {
        if (channel.scalingKeys.empty())
            return glm::vec3(1.0f);

        if (channel.scalingKeys.size() == 1)
            return channel.scalingKeys[0].scale;

        size_t i = 0;
        for (; i < channel.scalingKeys.size() - 1; ++i)
        {
            if (time < channel.scalingKeys[i + 1].time)
                break;
        }

        if (i >= channel.scalingKeys.size() - 1)
            return channel.scalingKeys.back().scale;

        const auto& k0 = channel.scalingKeys[i];
        const auto& k1 = channel.scalingKeys[i + 1];

        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = glm::clamp(t, 0.0f, 1.0f);

        return glm::mix(k0.scale, k1.scale, t);
    }

    float AnimationEvaluator::getDurationSeconds() const
    {
        if (!animationData) return 0.0f;
        float tps = animationData->ticksPerSecond > 0.0f ? animationData->ticksPerSecond : 24.0f;
        return animationData->duration / tps;
    }

    float AnimationEvaluator::secondsToTicks(float seconds) const
    {
        if (!animationData) return 0.0f;
        float tps = animationData->ticksPerSecond > 0.0f ? animationData->ticksPerSecond : 24.0f;
        return seconds * tps;
    }

    float AnimationEvaluator::ticksToSeconds(float ticks) const
    {
        if (!animationData) return 0.0f;
        float tps = animationData->ticksPerSecond > 0.0f ? animationData->ticksPerSecond : 24.0f;
        return ticks / tps;
    }
}
