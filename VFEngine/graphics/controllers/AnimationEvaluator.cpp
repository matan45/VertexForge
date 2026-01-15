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

        const size_t boneCount = animationData->skeleton.size();

        // Build bone to channel map for fast keyframe lookup
        buildBoneToChannelMap();

        // Initialize evaluated bones
        evaluatedBones.resize(boneCount);

        // Precompute bind poses from inverse bind poses
        computedBindPoses.resize(boneCount);
        computedLocalBindPoses.resize(boneCount);

        for (size_t i = 0; i < boneCount; ++i)
        {
            computedBindPoses[i] = glm::inverse(animationData->inverseBindPoses[i]);
        }

        // Compute local bind poses (relative to parent)
        for (size_t i = 0; i < boneCount; ++i)
        {
            const auto& bone = animationData->skeleton[i];
            if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(boneCount))
            {
                computedLocalBindPoses[i] = glm::inverse(computedBindPoses[bone.parentIndex]) * computedBindPoses[i];
            }
            else
            {
                computedLocalBindPoses[i] = computedBindPoses[i];
            }
        }

        loggerInfo("Loaded animation '{}': {} bones, {} channels",
                  animationData->name, boneCount, animationData->channels.size());
    }

    void AnimationEvaluator::clear()
    {
        animationData = nullptr;
        boneNameToChannelIndex.clear();
        evaluatedBones.clear();
        computedBindPoses.clear();
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
            return {};

        const size_t boneCount = animationData->skeleton.size();

        // Wrap time
        if (animationData->duration > 0.0f)
            timeInTicks = std::fmod(timeInTicks, animationData->duration);

        // Local transforms from keyframes
        for (size_t i = 0; i < boneCount; ++i)
        {
            auto it = boneNameToChannelIndex.find(animationData->skeleton[i].name);
            if (it != boneNameToChannelIndex.end())
            {
                const auto& ch = animationData->channels[it->second];
                int32_t parentIdx = animationData->skeleton[i].parentIndex;

                // Position: root uses animation, others use bind pose (bone length)
                glm::vec3 pos;
                if (parentIdx < 0 && !ch.positionKeys.empty())
                {
                    pos = interpolatePosition(ch, timeInTicks);
                }
                else
                {
                    pos = glm::vec3(computedLocalBindPoses[i][3]);
                }

                // Rotation: bind pose rotation * animation rotation
                glm::quat bindRot = glm::quat_cast(glm::mat3(computedLocalBindPoses[i]));
                glm::quat animRot = ch.rotationKeys.empty()
                    ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
                    : interpolateRotation(ch, timeInTicks);
                glm::quat rot = bindRot * animRot;

                // Scale from animation or default
                glm::vec3 scl = ch.scalingKeys.empty() ? glm::vec3(1.0f) : interpolateScale(ch, timeInTicks);

                // Store individual components for UI display
                evaluatedBones[i].position = pos;
                evaluatedBones[i].rotation = rot;
                evaluatedBones[i].scale = scl;
                evaluatedBones[i].localTransform = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scl);
            }
            else
            {
                // Extract components from bind pose for bones without animation
                evaluatedBones[i].position = glm::vec3(computedLocalBindPoses[i][3]);
                evaluatedBones[i].rotation = glm::quat_cast(glm::mat3(computedLocalBindPoses[i]));
                evaluatedBones[i].scale = glm::vec3(1.0f);
                evaluatedBones[i].localTransform = computedLocalBindPoses[i];
            }
        }

        // World transforms (parent * local)
        for (size_t i = 0; i < boneCount; ++i)
        {
            int parent = animationData->skeleton[i].parentIndex;
            if (parent >= 0)
                evaluatedBones[i].worldTransform = evaluatedBones[parent].worldTransform * evaluatedBones[i].localTransform;
            else
                evaluatedBones[i].worldTransform = evaluatedBones[i].localTransform;

            evaluatedBones[i].skinnedPosition = glm::vec3(evaluatedBones[i].worldTransform[3]);
        }

        // Final: globalInverse * world * inverseBindPose
        const glm::mat4& globalInv = animationData->globalInverseTransform;
        std::vector<glm::mat4> result(boneCount);
        for (size_t i = 0; i < boneCount; ++i)
        {
            result[i] = globalInv * evaluatedBones[i].worldTransform * animationData->inverseBindPoses[i];
            evaluatedBones[i].skinnedPosition = glm::vec3(globalInv * glm::vec4(evaluatedBones[i].skinnedPosition, 1.0f));
        }

        return result;
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
