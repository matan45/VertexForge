#include "AnimationEvaluator.hpp"
#include "print/Logger.hpp"
#include <algorithm>
#include <functional>

namespace controllers
{
    void AnimationEvaluator::loadAnimation(const resource::AnimationData& animation)
    {
        clear();

        animationData = &animation;

        if (!animationData->hasInverseBindPoses())
        {
            loggerError("Animation '{}' does not have inverse bind poses - cannot play",
                       animationData->name);
            return;
        }

        // Build bone to channel map for fast keyframe lookup
        buildBoneToChannelMap();

        // Initialize evaluated bones
        evaluatedBones.resize(animationData->skeleton.size());

        loggerInfo("Loaded animation '{}': {} bones, {} channels, {} inverse bind poses",
                  animationData->name,
                  animationData->skeleton.size(),
                  animationData->channels.size(),
                  animationData->inverseBindPoses.size());
    }

    void AnimationEvaluator::clear()
    {
        animationData = nullptr;
        boneNameToChannelIndex.clear();
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

    std::vector<glm::mat4> AnimationEvaluator::evaluatePose(float timeInTicks) const
    {
        if (!animationData || animationData->skeleton.empty() || !animationData->hasInverseBindPoses())
        {
            return {};
        }

        // Wrap time to animation duration
        float duration = animationData->duration;
        if (duration > 0.0f)
        {
            timeInTicks = std::fmod(timeInTicks, duration);
            if (timeInTicks < 0.0f) timeInTicks += duration;
        }

        // Evaluate local transforms for each bone
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const auto& animBone = animationData->skeleton[i];
            EvaluatedBone& eval = evaluatedBones[i];

            // Find animation channel for this bone
            auto it = boneNameToChannelIndex.find(animBone.name);

            glm::mat4 boneLocalTransform;
            if (it != boneNameToChannelIndex.end())
            {
                // Animated bone: interpolate keyframes
                const auto& channel = animationData->channels[it->second];

                glm::vec3 pos = interpolatePosition(channel, timeInTicks);
                glm::quat rot = interpolateRotation(channel, timeInTicks);
                glm::vec3 scl = interpolateScale(channel, timeInTicks);

                eval.position = pos;
                eval.rotation = rot;
                eval.scale = scl;

                glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
                glm::mat4 R = glm::mat4_cast(rot);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), scl);
                boneLocalTransform = T * R * S;
            }
            else
            {
                // Non-animated bone: use rest pose from skeleton
                boneLocalTransform = animBone.offsetMatrix;
                eval.position = glm::vec3(0.0f);
                eval.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                eval.scale = glm::vec3(1.0f);
            }

            eval.localTransform = boneLocalTransform;
        }

        // Compute world transforms
        computeWorldTransforms(false);

        // Build final bone matrices: worldTransform * inverseBindPose
        std::vector<glm::mat4> boneMatrices(animationData->skeleton.size(), glm::mat4(1.0f));

        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            const glm::mat4& worldTrans = evaluatedBones[i].worldTransform;
            const glm::mat4& invBind = animationData->inverseBindPoses[i];

            // Final bone matrix for GPU skinning
            boneMatrices[i] = worldTrans * invBind;

            // Store skinned position for debug visualization
            evaluatedBones[i].skinnedPosition = glm::vec3(boneMatrices[i][3]);
        }

        return boneMatrices;
    }

    void AnimationEvaluator::computeWorldTransforms(bool shouldLog) const
    {
        if (!animationData) return;

        // Track which bones have been computed (for topological ordering)
        std::vector<bool> computed(animationData->skeleton.size(), false);

        // Recursive lambda to compute world transform ensuring parent is computed first
        std::function<void(size_t)> computeBone = [&](size_t i) {
            if (computed[i]) return;

            const auto& animBone = animationData->skeleton[i];
            EvaluatedBone& eval = evaluatedBones[i];

            // Apply preTransform (accumulated transforms from non-bone ancestors)
            glm::mat4 localWithPreTransform = animBone.preTransform * eval.localTransform;

            // First compute parent if it exists and hasn't been computed
            if (animBone.parentIndex >= 0 &&
                animBone.parentIndex < static_cast<int32_t>(evaluatedBones.size()))
            {
                computeBone(static_cast<size_t>(animBone.parentIndex));
                eval.worldTransform =
                    evaluatedBones[animBone.parentIndex].worldTransform * localWithPreTransform;
            }
            else
            {
                // Root bone
                eval.worldTransform = localWithPreTransform;
            }

            computed[i] = true;

            if (shouldLog && i < 5)
            {
                loggerInfo("WORLD bone[{}] '{}': pos=({:.3f}, {:.3f}, {:.3f})",
                    i, animBone.name,
                    eval.worldTransform[3][0], eval.worldTransform[3][1], eval.worldTransform[3][2]);
            }
        };

        // Compute all bones
        for (size_t i = 0; i < animationData->skeleton.size(); ++i)
        {
            computeBone(i);
        }
    }

    glm::vec3 AnimationEvaluator::interpolatePosition(const resource::BoneAnimation& channel,
                                                       float time) const
    {
        if (channel.positionKeys.empty())
            return glm::vec3(0.0f);

        if (channel.positionKeys.size() == 1)
            return channel.positionKeys[0].position;

        // Find the two keyframes to interpolate between
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

        return glm::mix(channel.positionKeys[index0].position,
                       channel.positionKeys[index1].position,
                       factor);
    }

    glm::quat AnimationEvaluator::interpolateRotation(const resource::BoneAnimation& channel,
                                                       float time) const
    {
        if (channel.rotationKeys.empty())
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        if (channel.rotationKeys.size() == 1)
            return channel.rotationKeys[0].rotation;

        // Find the two keyframes to interpolate between
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

        return glm::slerp(channel.rotationKeys[index0].rotation,
                         channel.rotationKeys[index1].rotation,
                         factor);
    }

    glm::vec3 AnimationEvaluator::interpolateScale(const resource::BoneAnimation& channel,
                                                    float time) const
    {
        if (channel.scalingKeys.empty())
            return glm::vec3(1.0f);

        if (channel.scalingKeys.size() == 1)
            return channel.scalingKeys[0].scale;

        // Find the two keyframes to interpolate between
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

        return glm::mix(channel.scalingKeys[index0].scale,
                       channel.scalingKeys[index1].scale,
                       factor);
    }
}
