#pragma once

#include "AnimationExport.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <bitset>
#include "animator/AnimationLayerTypes.hpp"

namespace resource { struct SkeletonData; }

namespace animation
{
    struct EvaluatedBone; // full definition in AnimationEvaluator.hpp (per-bone local TRS)

    struct BlendedBone
    {
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    class VF_ANIMATION_API AnimationBlender
    {
    public:
        static std::vector<glm::mat4> blendPoses(
            const std::vector<glm::mat4>& poseA,
            const std::vector<glm::mat4>& poseB,
            float blendWeight);

        static std::vector<glm::mat4> blendNPoses(
            const std::vector<std::vector<glm::mat4>>& poses,
            const std::vector<float>& weights);

        static BlendedBone blendBoneTransforms(
            const glm::vec3& posA, const glm::quat& rotA, const glm::vec3& scaleA,
            const glm::vec3& posB, const glm::quat& rotB, const glm::vec3& scaleB,
            float blendWeight);

        static void blendPosesWithMask(
            std::vector<glm::mat4>& basePose,
            const std::vector<glm::mat4>& layerPose,
            float weight,
            const animator::BoneMask& mask,
            bool hasMask);

        static void additivePoseBlend(
            std::vector<glm::mat4>& basePose,
            const std::vector<glm::mat4>& additivePose,
            float weight,
            const animator::BoneMask& mask,
            bool hasMask,
            const std::vector<glm::mat4>& bindPoses);

        // VK-1441: blend two states' animated LOCAL TRS (from AnimationEvaluator::getEvaluatedBones())
        // then run ONE hierarchy + inverse-bind pass, so a child bone stays attached to its parent
        // during a cross-fade. Replaces blending the final skinning palettes per-bone in isolation,
        // which severed the parent->child relationship and stretched/sheared limbs mid-blend.
        static std::vector<glm::mat4> blendLocalPoses(
            const std::vector<EvaluatedBone>& bonesA,
            const std::vector<EvaluatedBone>& bonesB,
            float blendWeight,
            const resource::SkeletonData& skeleton);

        // VK-1441: N-way local-space blend for blend trees. Mirrors blendNPoses' nlerp + shortest-path
        // handling, but on animated LOCAL TRS, then composes the skinning palette once.
        static std::vector<glm::mat4> blendLocalNPoses(
            const std::vector<std::vector<EvaluatedBone>>& sources,
            const std::vector<float>& weights,
            const resource::SkeletonData& skeleton);

    private:
        static void decomposeMatrix(const glm::mat4& matrix,
                                    glm::vec3& position,
                                    glm::quat& rotation,
                                    glm::vec3& scale);

        static glm::mat4 composeMatrix(const glm::vec3& position,
                                       const glm::quat& rotation,
                                       const glm::vec3& scale);
    };
}
