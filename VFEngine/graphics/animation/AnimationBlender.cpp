#include "AnimationBlender.hpp"
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
                    blendedRot = rot * w;
                    firstQuat = false;
                }
                else
                {
                    // Ensure shortest path
                    if (glm::dot(blendedRot, rot) < 0.0f)
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
