#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Skeleton/Skeleton.h>
#include <Jolt/Skeleton/SkeletonPose.h>
#include <Jolt/Core/Reference.h>
#include "resource/Types.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <unordered_map>

namespace core::physics
{
    struct SkeletonConversionResult
    {
        JPH::Ref<JPH::Skeleton> physicsSkeleton;
        std::vector<int> physicsToAnimBoneIndex;
        std::unordered_map<int, int> animToPhysicsBoneIndex;

        bool isValid() const { return physicsSkeleton != nullptr && !physicsToAnimBoneIndex.empty(); }
    };

    class PhysicsSkeletonConverter
    {
    public:
        static SkeletonConversionResult convertToPhysicsSkeleton(
            const resource::SkeletonData& skeletonData,
            const types::PhysicsAnimationConfig& config);

        static JPH::Ref<JPH::Skeleton> convertFullSkeleton(
            const resource::SkeletonData& skeletonData);

        static JPH::SkeletonPose buildPhysicsSkeletonPose(
            const JPH::Skeleton* physicsSkeleton,
            const std::vector<glm::mat4>& animBoneWorldTransforms,
            const std::vector<int>& physicsToAnimBoneIndex,
            const glm::vec3& entityPosition = glm::vec3(0.0f));

        // Animator output is skinning matrices (globalInverse * boneModel * inverseBind);
        // this recovers model space before building the pose
        static JPH::SkeletonPose buildTargetPoseFromAnimatorMatrices(
            const JPH::Skeleton* physicsSkeleton,
            const std::vector<glm::mat4>& animatorSkinningMatrices,
            const std::vector<int>& physicsToAnimBoneIndex,
            const resource::SkeletonData& skeletonData,
            const glm::vec3& entityPosition = glm::vec3(0.0f));

        static std::vector<glm::mat4> ragdollPoseToSkinningMatrices(
            const JPH::SkeletonPose& ragdollPose,
            const SkeletonConversionResult& conversion,
            const resource::SkeletonData& skeletonData,
            const std::vector<glm::mat4>& fallbackAnimWorldTransforms);

        static glm::mat4 toGlmMat4(const JPH::Mat44& m);
        static JPH::Mat44 toJoltMat44(const glm::mat4& m);
    };
}
