#include "AnimationBlender.hpp"
#include "print/Logger.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>

namespace animation
{
    void AnimationBlender::loadSourceAnimation(const resource::AnimationData& animation, const resource::SkeletonData& skeleton)
    {
        sourceEvaluator.loadAnimation(animation, skeleton);
    }

    void AnimationBlender::loadTargetAnimation(const resource::AnimationData& animation, const resource::SkeletonData& skeleton)
    {
        targetEvaluator.loadAnimation(animation, skeleton);
    }

    void AnimationBlender::clearSource()
    {
        sourceEvaluator.clear();
    }

    void AnimationBlender::clearTarget()
    {
        targetEvaluator.clear();
    }

    void AnimationBlender::clear()
    {
        sourceEvaluator.clear();
        targetEvaluator.clear();
    }

    size_t AnimationBlender::getBoneCount() const
    {
        if (hasSourceAnimation())
        {
            return sourceEvaluator.getEvaluatedBones().size();
        }
        if (hasTargetAnimation())
        {
            return targetEvaluator.getEvaluatedBones().size();
        }
        return 0;
    }

    std::vector<glm::mat4> AnimationBlender::evaluateBlendedPose(float sourceTime, float targetTime, float blendWeight) const
    {
        // Clamp blend weight
        blendWeight = glm::clamp(blendWeight, 0.0f, 1.0f);

        // If blend weight is 0, just return source pose
        if (blendWeight <= 0.0f && hasSourceAnimation())
        {
            float sourceTimeInTicks = sourceEvaluator.secondsToTicks(sourceTime);
            return sourceEvaluator.evaluatePose(sourceTimeInTicks);
        }

        // If blend weight is 1, just return target pose
        if (blendWeight >= 1.0f && hasTargetAnimation())
        {
            float targetTimeInTicks = targetEvaluator.secondsToTicks(targetTime);
            return targetEvaluator.evaluatePose(targetTimeInTicks);
        }

        // If only source is available
        if (hasSourceAnimation() && !hasTargetAnimation())
        {
            float sourceTimeInTicks = sourceEvaluator.secondsToTicks(sourceTime);
            return sourceEvaluator.evaluatePose(sourceTimeInTicks);
        }

        // If only target is available
        if (!hasSourceAnimation() && hasTargetAnimation())
        {
            float targetTimeInTicks = targetEvaluator.secondsToTicks(targetTime);
            return targetEvaluator.evaluatePose(targetTimeInTicks);
        }

        // Neither animation available
        if (!hasSourceAnimation() && !hasTargetAnimation())
        {
            return {};
        }

        // Both animations available - perform blend
        float sourceTimeInTicks = sourceEvaluator.secondsToTicks(sourceTime);
        float targetTimeInTicks = targetEvaluator.secondsToTicks(targetTime);

        std::vector<glm::mat4> sourcePose = sourceEvaluator.evaluatePose(sourceTimeInTicks);
        std::vector<glm::mat4> targetPose = targetEvaluator.evaluatePose(targetTimeInTicks);

        return blendPoses(sourcePose, targetPose, blendWeight);
    }

    std::vector<glm::mat4> AnimationBlender::blendPoses(
        const std::vector<glm::mat4>& poseA,
        const std::vector<glm::mat4>& poseB,
        float blendWeight)
    {
        if (poseA.empty())
            return poseB;
        if (poseB.empty())
            return poseA;

        // Use the smaller of the two sizes to avoid out-of-bounds
        size_t boneCount = std::min(poseA.size(), poseB.size());
        std::vector<glm::mat4> result(boneCount);

        blendWeight = glm::clamp(blendWeight, 0.0f, 1.0f);

        // If blend weight is at extremes, return the appropriate pose
        if (blendWeight <= 0.0f)
        {
            for (size_t i = 0; i < boneCount; ++i)
                result[i] = poseA[i];
            return result;
        }
        if (blendWeight >= 1.0f)
        {
            for (size_t i = 0; i < boneCount; ++i)
                result[i] = poseB[i];
            return result;
        }

        // Blend each bone
        for (size_t i = 0; i < boneCount; ++i)
        {
            glm::vec3 posA, posB;
            glm::quat rotA, rotB;
            glm::vec3 scaleA, scaleB;

            decomposeMatrix(poseA[i], posA, rotA, scaleA);
            decomposeMatrix(poseB[i], posB, rotB, scaleB);

            BlendedBone blended = blendBoneTransforms(posA, rotA, scaleA, posB, rotB, scaleB, blendWeight);
            result[i] = composeMatrix(blended.position, blended.rotation, blended.scale);
        }

        return result;
    }

    BlendedBone AnimationBlender::blendBoneTransforms(
        const glm::vec3& posA, const glm::quat& rotA, const glm::vec3& scaleA,
        const glm::vec3& posB, const glm::quat& rotB, const glm::vec3& scaleB,
        float blendWeight)
    {
        BlendedBone result;

        // Linear interpolation for position
        result.position = glm::mix(posA, posB, blendWeight);

        // Spherical linear interpolation for rotation
        result.rotation = glm::slerp(rotA, rotB, blendWeight);

        // Linear interpolation for scale
        result.scale = glm::mix(scaleA, scaleB, blendWeight);

        return result;
    }

    void AnimationBlender::decomposeMatrix(const glm::mat4& matrix,
                                           glm::vec3& position,
                                           glm::quat& rotation,
                                           glm::vec3& scale)
    {
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(matrix, scale, rotation, position, skew, perspective);
    }

    glm::mat4 AnimationBlender::composeMatrix(const glm::vec3& position,
                                              const glm::quat& rotation,
                                              const glm::vec3& scale)
    {
        glm::mat4 result = glm::mat4(1.0f);
        result = glm::translate(result, position);
        result = result * glm::mat4_cast(rotation);
        result = glm::scale(result, scale);
        return result;
    }
}
