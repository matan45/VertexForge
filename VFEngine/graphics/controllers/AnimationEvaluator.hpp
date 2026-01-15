#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "resource/Types.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace controllers
{
    // Evaluated transform for a single bone
    struct EvaluatedBone
    {
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 localTransform{1.0f};
        glm::mat4 worldTransform{1.0f};
        glm::vec3 skinnedPosition{0.0f};  // Final position after skinning transform
    };

    // Evaluates animation poses and computes final bone matrices for GPU skinning
    // Self-contained: uses animation's own skeleton and inverse bind poses
    class AnimationEvaluator
    {
    public:
        AnimationEvaluator() = default;
        ~AnimationEvaluator() = default;

        // Load animation data (v0.0.6+ format with inverse bind poses)
        void loadAnimation(const resource::AnimationData& animation);

        // Clear loaded data
        void clear();

        // Evaluate animation at given time (in ticks) and compute final bone matrices
        // Returns vector of matrices ready for GPU upload (includes inverse bind pose)
        std::vector<glm::mat4> evaluatePose(float timeInTicks) const;

        // Get evaluated bone transforms (before inverse bind pose multiplication)
        const std::vector<EvaluatedBone>& getEvaluatedBones() const { return evaluatedBones; }

        // Animation info accessors
        float getDuration() const { return animationData ? animationData->duration : 0.0f; }
        float getTicksPerSecond() const { return animationData ? animationData->ticksPerSecond : 24.0f; }
        float getDurationSeconds() const;
        size_t getBoneCount() const { return animationData ? animationData->skeleton.size() : 0; }
        bool isLoaded() const { return animationData != nullptr && animationData->hasInverseBindPoses(); }

        // Convert between time formats
        float secondsToTicks(float seconds) const;
        float ticksToSeconds(float ticks) const;

    private:
        // Keyframe interpolation helpers
        glm::vec3 interpolatePosition(const resource::BoneAnimation& channel, float time) const;
        glm::quat interpolateRotation(const resource::BoneAnimation& channel, float time) const;
        glm::vec3 interpolateScale(const resource::BoneAnimation& channel, float time) const;

        // Compute world transforms from local transforms (parent-child hierarchy)
        void computeWorldTransforms(bool shouldLog = false) const;

        // Map bone names to animation channel indices for fast lookup
        void buildBoneToChannelMap();

        // Pointer to loaded data (not owned)
        const resource::AnimationData* animationData = nullptr;

        // Bone name to animation channel index
        std::unordered_map<std::string, size_t> boneNameToChannelIndex;

        // Evaluated bone transforms (mutable for const evaluation)
        mutable std::vector<EvaluatedBone> evaluatedBones;
    };
}
