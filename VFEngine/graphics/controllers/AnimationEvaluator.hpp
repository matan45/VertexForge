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
    };

    // Evaluates animation poses and computes final bone matrices for GPU skinning
    class AnimationEvaluator
    {
    public:
        AnimationEvaluator() = default;
        ~AnimationEvaluator() = default;

        // Load animation and mesh skeleton data
        // The mesh skeleton provides inverse bind poses, animation provides keyframes
        void loadAnimation(const resource::AnimationData& animation,
                          const resource::SkeletonInfo& meshSkeleton);

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
        size_t getBoneCount() const { return meshSkeleton ? meshSkeleton->boneCount() : 0; }
        bool isLoaded() const { return animationData != nullptr && meshSkeleton != nullptr; }

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

        // Map mesh skeleton bone names to animation skeleton indices
        void buildSkeletonMapping();

        // Pointers to loaded data (not owned)
        const resource::AnimationData* animationData = nullptr;
        const resource::SkeletonInfo* meshSkeleton = nullptr;

        // Bone name to animation channel index
        std::unordered_map<std::string, size_t> boneNameToChannelIndex;

        // Mesh skeleton bone index to animation skeleton bone index
        // Used to map from mesh bone order to animation bone order
        std::vector<int32_t> meshBoneToAnimBone;

        // Animation skeleton bone index to mesh skeleton bone index
        std::vector<int32_t> animBoneToMeshBone;

        // Bind pose alignment: corrects for differences between animation rest pose and mesh bind pose
        // Stored per mesh bone index
        std::vector<glm::mat4> bindPoseCorrection;

        // Evaluated bone transforms (mutable for const evaluation)
        mutable std::vector<EvaluatedBone> evaluatedBones;

        // Helper: compute world transform for a bone using animation skeleton's offsetMatrix (rest pose)
        glm::mat4 computeRestPoseWorldTransform(size_t animBoneIdx) const;
    };
}
