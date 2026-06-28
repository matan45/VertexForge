#include "AnimationBlender.hpp"
#include "AnimationEvaluator.hpp" // EvaluatedBone + composeSkinningPalette + resource::SkeletonData
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>

namespace animation
{
    std::vector<glm::mat4> AnimationBlender::blendPoses(
        const std::vector<glm::mat4>& poseA,
        const std::vector<glm::mat4>& poseB,
        float blendWeight)
    {
        if (poseA.empty())
            return poseB;
        if (poseB.empty())
            return poseA;

        size_t boneCount = std::min(poseA.size(), poseB.size());
        std::vector<glm::mat4> result(boneCount);

        blendWeight = glm::clamp(blendWeight, 0.0f, 1.0f);

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

    std::vector<glm::mat4> AnimationBlender::blendNPoses(
        const std::vector<std::vector<glm::mat4>>& poses,
        const std::vector<float>& weights)
    {
        if (poses.empty() || weights.empty())
            return {};

        // Find first non-zero weight pose to use as base
        size_t boneCount = 0;
        for (const auto& pose : poses)
        {
            if (!pose.empty())
            {
                boneCount = pose.size();
                break;
            }
        }

        if (boneCount == 0)
            return {};

        // Single active pose optimization
        int activeCount = 0;
        size_t lastActive = 0;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            if (weights[i] > 0.001f && i < poses.size() && !poses[i].empty())
            {
                activeCount++;
                lastActive = i;
            }
        }

        if (activeCount == 0)
            return poses.empty() ? std::vector<glm::mat4>{} : poses[0];
        if (activeCount == 1)
            return poses[lastActive];

        std::vector<glm::mat4> result(boneCount);

        for (size_t bone = 0; bone < boneCount; ++bone)
        {
            glm::vec3 blendedPos{0.0f};
            glm::vec3 blendedScale{0.0f};
            glm::quat blendedRot{0.0f, 0.0f, 0.0f, 0.0f};
            glm::quat referenceRot;
            bool firstQuat = true;

            for (size_t p = 0; p < poses.size() && p < weights.size(); ++p)
            {
                float w = weights[p];
                if (w <= 0.001f || poses[p].empty() || bone >= poses[p].size())
                    continue;

                glm::vec3 pos, scale;
                glm::quat rot;
                decomposeMatrix(poses[p][bone], pos, rot, scale);

                blendedPos += pos * w;
                blendedScale += scale * w;

                if (firstQuat)
                {
                    referenceRot = rot;
                    blendedRot = rot * w;
                    firstQuat = false;
                }
                else
                {
                    if (glm::dot(referenceRot, rot) < 0.0f)
                        rot = -rot;
                    blendedRot = blendedRot + rot * w;
                }
            }

            float rotLen = glm::length(blendedRot);
            if (rotLen > 0.0001f)
                blendedRot = blendedRot / rotLen;
            else
                blendedRot = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};

            result[bone] = composeMatrix(blendedPos, blendedRot, blendedScale);
        }

        return result;
    }

    BlendedBone AnimationBlender::blendBoneTransforms(
        const glm::vec3& posA, const glm::quat& rotA, const glm::vec3& scaleA,
        const glm::vec3& posB, const glm::quat& rotB, const glm::vec3& scaleB,
        float blendWeight)
    {
        BlendedBone result;
        result.position = glm::mix(posA, posB, blendWeight);
        result.rotation = glm::slerp(rotA, rotB, blendWeight);
        result.scale = glm::mix(scaleA, scaleB, blendWeight);
        return result;
    }

    void AnimationBlender::blendPosesWithMask(
        std::vector<glm::mat4>& basePose,
        const std::vector<glm::mat4>& layerPose,
        float weight,
        const animator::BoneMask& mask,
        bool hasMask)
    {
        if (layerPose.empty() || basePose.empty())
            return;

        size_t boneCount = std::min(basePose.size(), layerPose.size());
        weight = glm::clamp(weight, 0.0f, 1.0f);

        if (weight <= 0.001f)
            return;

        for (size_t i = 0; i < boneCount; ++i)
        {
            if (hasMask && !mask.test(i))
                continue;

            if (weight >= 0.999f)
            {
                basePose[i] = layerPose[i];
            }
            else
            {
                glm::vec3 posA, posB;
                glm::quat rotA, rotB;
                glm::vec3 scaleA, scaleB;

                decomposeMatrix(basePose[i], posA, rotA, scaleA);
                decomposeMatrix(layerPose[i], posB, rotB, scaleB);

                BlendedBone blended = blendBoneTransforms(posA, rotA, scaleA, posB, rotB, scaleB, weight);
                basePose[i] = composeMatrix(blended.position, blended.rotation, blended.scale);
            }
        }
    }

    void AnimationBlender::additivePoseBlend(
        std::vector<glm::mat4>& basePose,
        const std::vector<glm::mat4>& additivePose,
        float weight,
        const animator::BoneMask& mask,
        bool hasMask,
        const std::vector<glm::mat4>& bindPoses)
    {
        if (additivePose.empty() || basePose.empty() || bindPoses.empty())
            return;

        size_t boneCount = std::min({basePose.size(), additivePose.size(), bindPoses.size()});
        weight = glm::clamp(weight, 0.0f, 1.0f);

        if (weight <= 0.001f)
            return;

        for (size_t i = 0; i < boneCount; ++i)
        {
            if (hasMask && !mask.test(i))
                continue;

            glm::vec3 basePos, additivePos, bindPos;
            glm::quat baseRot, additiveRot, bindRot;
            glm::vec3 baseScale, additiveScale, bindScale;

            decomposeMatrix(basePose[i], basePos, baseRot, baseScale);
            decomposeMatrix(additivePose[i], additivePos, additiveRot, additiveScale);
            decomposeMatrix(bindPoses[i], bindPos, bindRot, bindScale);

            // Compute additive delta: difference from bind pose
            glm::vec3 deltaPos = (additivePos - bindPos) * weight;
            glm::quat deltaRot = additiveRot * glm::inverse(bindRot);
            deltaRot = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), deltaRot, weight);

            glm::vec3 deltaScale(1.0f);
            if (glm::length(bindScale) > 0.0001f)
            {
                glm::vec3 scaleRatio = additiveScale / bindScale;
                deltaScale = glm::mix(glm::vec3(1.0f), scaleRatio, weight);
            }

            // Apply delta to base
            glm::vec3 resultPos = basePos + deltaPos;
            glm::quat resultRot = deltaRot * baseRot;
            glm::vec3 resultScale = baseScale * deltaScale;

            basePose[i] = composeMatrix(resultPos, resultRot, resultScale);
        }
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

    std::vector<glm::mat4> AnimationBlender::blendLocalPoses(
        const std::vector<EvaluatedBone>& bonesA,
        const std::vector<EvaluatedBone>& bonesB,
        float blendWeight,
        const resource::SkeletonData& skeleton)
    {
        const size_t boneCount = std::min({bonesA.size(), bonesB.size(), skeleton.bones.size()});
        if (boneCount == 0)
            return {};

        blendWeight = glm::clamp(blendWeight, 0.0f, 1.0f);

        // Blend each bone's ANIMATED local TRS (parent-relative), then rebuild the local transform
        // exactly as evaluatePose does (preTransform * T*R*S). glm::mix/slerp return the endpoints
        // exactly at blendWeight 0/1, so those frames stay identical to the single-pose path.
        std::vector<glm::mat4> localTransforms(boneCount);
        for (size_t i = 0; i < boneCount; ++i)
        {
            BlendedBone blended = blendBoneTransforms(
                bonesA[i].position, bonesA[i].rotation, bonesA[i].scale,
                bonesB[i].position, bonesB[i].rotation, bonesB[i].scale,
                blendWeight);
            localTransforms[i] = skeleton.bones[i].preTransform *
                                 composeMatrix(blended.position, blended.rotation, blended.scale);
        }

        return composeSkinningPalette(localTransforms, skeleton);
    }

    std::vector<glm::mat4> AnimationBlender::blendLocalNPoses(
        const std::vector<std::vector<EvaluatedBone>>& sources,
        const std::vector<float>& weights,
        const resource::SkeletonData& skeleton)
    {
        if (sources.empty() || weights.empty())
            return {};

        // Bone count = first non-empty source, bounded by the skeleton.
        size_t boneCount = 0;
        size_t firstNonEmpty = sources.size();
        for (size_t p = 0; p < sources.size(); ++p)
        {
            if (!sources[p].empty())
            {
                boneCount = sources[p].size();
                firstNonEmpty = p;
                break;
            }
        }
        boneCount = std::min(boneCount, skeleton.bones.size());
        if (boneCount == 0)
            return {};

        // Builds a single source's local transforms (preTransform * T*R*S per bone).
        auto buildLocals = [&](size_t s)
        {
            std::vector<glm::mat4> locals(boneCount);
            for (size_t i = 0; i < boneCount; ++i)
                locals[i] = skeleton.bones[i].preTransform *
                            composeMatrix(sources[s][i].position, sources[s][i].rotation, sources[s][i].scale);
            return locals;
        };

        // Single active-weight optimization (mirrors blendNPoses): no blend needed.
        int activeCount = 0;
        size_t lastActive = firstNonEmpty;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            if (weights[i] > 0.001f && i < sources.size() && !sources[i].empty())
            {
                activeCount++;
                lastActive = i;
            }
        }
        if (activeCount == 0)
            return composeSkinningPalette(buildLocals(firstNonEmpty), skeleton);
        if (activeCount == 1)
            return composeSkinningPalette(buildLocals(lastActive), skeleton);

        std::vector<glm::mat4> localTransforms(boneCount);
        for (size_t bone = 0; bone < boneCount; ++bone)
        {
            glm::vec3 blendedPos{0.0f};
            glm::vec3 blendedScale{0.0f};
            glm::quat blendedRot{0.0f, 0.0f, 0.0f, 0.0f};
            glm::quat referenceRot;
            bool firstQuat = true;

            for (size_t p = 0; p < sources.size() && p < weights.size(); ++p)
            {
                float w = weights[p];
                if (w <= 0.001f || sources[p].empty() || bone >= sources[p].size())
                    continue;

                const EvaluatedBone& eb = sources[p][bone];
                blendedPos += eb.position * w;
                blendedScale += eb.scale * w;

                glm::quat rot = eb.rotation;
                if (firstQuat)
                {
                    referenceRot = rot;
                    blendedRot = rot * w;
                    firstQuat = false;
                }
                else
                {
                    if (glm::dot(referenceRot, rot) < 0.0f)
                        rot = -rot;
                    blendedRot = blendedRot + rot * w;
                }
            }

            float rotLen = glm::length(blendedRot);
            if (rotLen > 0.0001f)
                blendedRot = blendedRot / rotLen;
            else
                blendedRot = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};

            localTransforms[bone] = skeleton.bones[bone].preTransform *
                                    composeMatrix(blendedPos, blendedRot, blendedScale);
        }

        return composeSkinningPalette(localTransforms, skeleton);
    }
}
