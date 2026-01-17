#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "resource/Types.hpp"
#include "AnimationEvaluator.hpp"
#include <vector>
#include <memory>

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
        AnimationBlender() = default;
        ~AnimationBlender() = default;

        // Load animations for blending (skeleton required for pose evaluation)
        void loadSourceAnimation(const resource::AnimationData& animation, const resource::SkeletonData& skeleton);
        void loadTargetAnimation(const resource::AnimationData& animation, const resource::SkeletonData& skeleton);

        // Clear loaded animations
        void clearSource();
        void clearTarget();
        void clear();

        // Evaluate blended pose
        // sourceTime and targetTime are in seconds
        // blendWeight: 0.0 = full source, 1.0 = full target
        std::vector<glm::mat4> evaluateBlendedPose(float sourceTime, float targetTime, float blendWeight) const;

        // Static utility for blending two existing pose arrays
        static std::vector<glm::mat4> blendPoses(
            const std::vector<glm::mat4>& poseA,
            const std::vector<glm::mat4>& poseB,
            float blendWeight);

        // Static utility for blending bone transforms (position, rotation, scale)
        static BlendedBone blendBoneTransforms(
            const glm::vec3& posA, const glm::quat& rotA, const glm::vec3& scaleA,
            const glm::vec3& posB, const glm::quat& rotB, const glm::vec3& scaleB,
            float blendWeight);

        // Check if animations are loaded
        bool hasSourceAnimation() const { return sourceEvaluator.isLoaded(); }
        bool hasTargetAnimation() const { return targetEvaluator.isLoaded(); }
        bool canBlend() const { return hasSourceAnimation() && hasTargetAnimation(); }

        // Get bone count (assumes both animations have same skeleton)
        size_t getBoneCount() const;

        // Access evaluators for advanced use
        const AnimationEvaluator& getSourceEvaluator() const { return sourceEvaluator; }
        const AnimationEvaluator& getTargetEvaluator() const { return targetEvaluator; }

    private:
        AnimationEvaluator sourceEvaluator;
        AnimationEvaluator targetEvaluator;

        // Decompose a transformation matrix into position, rotation, scale
        static void decomposeMatrix(const glm::mat4& matrix,
                                    glm::vec3& position,
                                    glm::quat& rotation,
                                    glm::vec3& scale);

        // Compose position, rotation, scale back into a matrix
        static glm::mat4 composeMatrix(const glm::vec3& position,
                                       const glm::quat& rotation,
                                       const glm::vec3& scale);
    };
}
