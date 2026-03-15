#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <bitset>
#include "animator/AnimationLayerTypes.hpp"

namespace animation
{
    struct BlendedBone
    {
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    class AnimationBlender
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
