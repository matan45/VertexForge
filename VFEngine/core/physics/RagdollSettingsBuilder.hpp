#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Core/Reference.h>
#include "PhysicsSkeletonConverter.hpp"
#include "resource/Types.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

namespace core::physics
{
    struct RagdollBuildResult
    {
        JPH::Ref<JPH::RagdollSettings> settings;
        SkeletonConversionResult skeletonConversion;
        bool success = false;
        std::string errorMessage;
    };

    class RagdollSettingsBuilder
    {
    public:
        static RagdollBuildResult build(
            const types::PhysicsAnimationConfig& config,
            const resource::SkeletonData& skeletonData,
            const glm::vec3& entityWorldPosition = glm::vec3(0.0f),
            const glm::quat& entityWorldRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    private:
        static JPH::Ref<JPH::Shape> createBoneShape(const types::BoneBodyMapping& mapping);

        static JPH::Ref<JPH::SwingTwistConstraintSettings> createJointConstraint(
            const types::JointConstraintLimits* limits,
            const JPH::Mat44& childWorldTransform,
            const JPH::Mat44& parentWorldTransform);
    };
}
