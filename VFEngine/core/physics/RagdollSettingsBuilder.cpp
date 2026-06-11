#include "RagdollSettingsBuilder.hpp"
#include "JoltConversions.hpp"
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace core::physics
{
    RagdollBuildResult RagdollSettingsBuilder::build(
        const types::PhysicsAnimationConfig& config,
        const resource::SkeletonData& skeletonData,
        const glm::vec3& entityWorldPosition,
        const glm::quat& entityWorldRotation)
    {
        RagdollBuildResult result;

        if (config.boneBodyMappings.empty())
        {
            result.errorMessage = "No bone body mappings configured";
            return result;
        }

        if (!skeletonData.hasBones())
        {
            result.errorMessage = "Skeleton has no bones";
            return result;
        }

        result.skeletonConversion = PhysicsSkeletonConverter::convertToPhysicsSkeleton(skeletonData, config);
        if (!result.skeletonConversion.isValid())
        {
            result.errorMessage = "Failed to convert skeleton - no valid bone mappings found";
            return result;
        }

        glm::mat4 entityTransform = glm::translate(glm::mat4(1.0f), entityWorldPosition) * glm::mat4_cast(entityWorldRotation);

        result.settings = new JPH::RagdollSettings();
        result.settings->mSkeleton = result.skeletonConversion.physicsSkeleton;

        int jointCount = result.skeletonConversion.physicsSkeleton->GetJointCount();
        result.settings->mParts.resize(jointCount);

        std::vector<JPH::Mat44> boneWorldMatrices(jointCount);

        for (int p = 0; p < jointCount; ++p)
        {
            int animBoneIdx = result.skeletonConversion.physicsToAnimBoneIndex[p];
            const auto& bone = skeletonData.bones[animBoneIdx];

            const types::BoneBodyMapping* mapping = config.findMapping(bone.name);
            if (!mapping)
            {
                result.errorMessage = "Missing mapping for bone: " + bone.name;
                return result;
            }

            // bindPoses[i] is the bone's world-space transform at bind time
            glm::mat4 boneWorld = entityTransform * skeletonData.bindPoses[animBoneIdx];
            JPH::Mat44 joltBoneWorld = PhysicsSkeletonConverter::toJoltMat44(boneWorld);
            boneWorldMatrices[p] = joltBoneWorld;

            glm::vec3 bonePos = glm::vec3(boneWorld[3]);
            glm::quat boneRot = glm::normalize(glm::quat_cast(boneWorld));

            JPH::Ref<JPH::Shape> shape = createBoneShape(*mapping);
            if (shape == nullptr)
            {
                result.errorMessage = "Failed to create shape for bone: " + bone.name;
                return result;
            }

            bool hasOffset = glm::length(mapping->offset) > 0.001f;
            bool hasRotOffset = glm::abs(mapping->rotationOffset.w - 1.0f) > 0.001f ||
                                glm::length(glm::vec3(mapping->rotationOffset.x, mapping->rotationOffset.y, mapping->rotationOffset.z)) > 0.001f;

            if (hasOffset || hasRotOffset)
            {
                JPH::RotatedTranslatedShapeSettings offsetSettings(
                    toJolt(mapping->offset),
                    toJolt(mapping->rotationOffset),
                    shape);
                auto offsetResult = offsetSettings.Create();
                if (offsetResult.IsValid())
                {
                    shape = offsetResult.Get();
                }
            }

            auto& part = result.settings->mParts[p];
            part.SetShape(shape);
            part.mPosition = toJoltR(bonePos);
            part.mRotation = toJolt(boneRot);
            part.mMotionType = JPH::EMotionType::Dynamic;
            uint8_t layer = (mapping->collisionLayer != 255) ? mapping->collisionLayer : config.collisionLayer;
            part.mObjectLayer = static_cast<JPH::ObjectLayer>(layer);
            part.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            part.mMassPropertiesOverride.mMass = mapping->mass;
            part.mFriction = mapping->friction;
            part.mRestitution = mapping->restitution;
            part.mLinearDamping = 0.1f;
            part.mAngularDamping = 0.1f;

            int physicsParentIdx = result.skeletonConversion.physicsSkeleton->GetJoint(p).mParentJointIndex;
            if (physicsParentIdx >= 0)
            {
                const types::JointConstraintLimits* limits = config.findJointLimits(bone.name);

                // Spring frequency/damping are fixed at build time; torque limits are
                // rescaled per frame by the powered-ragdoll drive loop
                const types::BoneMotorSettings* motor = config.findBoneMotor(bone.name);
                float frequency = motor ? motor->frequency : config.defaultMotorFrequency;
                float damping = motor ? motor->damping : config.defaultMotorDamping;
                float maxTorque = motor ? motor->maxTorque : config.defaultMotorMaxTorque;
                JPH::MotorSettings motorSettings(frequency, damping, 0.0f, maxTorque);

                part.mToParent = createJointConstraint(limits, joltBoneWorld, boneWorldMatrices[physicsParentIdx],
                                                       motorSettings);
            }
        }

        result.settings->Stabilize();
        result.settings->DisableParentChildCollisions(boneWorldMatrices.data(), 0.0f);
        result.settings->CalculateBodyIndexToConstraintIndex();

        result.success = true;
        return result;
    }

    JPH::Ref<JPH::Shape> RagdollSettingsBuilder::createBoneShape(const types::BoneBodyMapping& mapping)
    {
        switch (mapping.shape)
        {
        case types::ColliderShape::Box:
        {
            glm::vec3 halfExtents = glm::max(mapping.size, glm::vec3(0.001f));
            return new JPH::BoxShape(toJolt(halfExtents));
        }
        case types::ColliderShape::Sphere:
        {
            float radius = std::max(mapping.size.x, 0.001f);
            return new JPH::SphereShape(radius);
        }
        case types::ColliderShape::Capsule:
        {
            float radius = std::max(mapping.size.x, 0.001f);
            float halfHeight = std::max(mapping.size.y - radius, 0.001f);
            return new JPH::CapsuleShape(halfHeight, radius);
        }
        default:
        {
            // Default to capsule for unsupported shapes
            float radius = std::max(mapping.size.x, 0.001f);
            float halfHeight = std::max(mapping.size.y - radius, 0.001f);
            return new JPH::CapsuleShape(halfHeight, radius);
        }
        }
    }

    JPH::Ref<JPH::SwingTwistConstraintSettings> RagdollSettingsBuilder::createJointConstraint(
        const types::JointConstraintLimits* limits,
        const JPH::Mat44& childWorldTransform,
        const JPH::Mat44& parentWorldTransform,
        const JPH::MotorSettings& motorSettings)
    {
        JPH::Ref<JPH::SwingTwistConstraintSettings> constraint = new JPH::SwingTwistConstraintSettings();

        constraint->mSwingMotorSettings = motorSettings;
        constraint->mTwistMotorSettings = motorSettings;

        constraint->mSpace = JPH::EConstraintSpace::WorldSpace;

        // Constraint position: at the child bone pivot
        JPH::Vec3 childPos = childWorldTransform.GetTranslation();
        constraint->mPosition1 = JPH::RVec3(childPos.GetX(), childPos.GetY(), childPos.GetZ());
        constraint->mPosition2 = constraint->mPosition1;

        // Twist axis: direction from parent to child
        JPH::Vec3 parentPos = parentWorldTransform.GetTranslation();
        JPH::Vec3 twistAxis = (childPos - parentPos);
        float len = twistAxis.Length();
        if (len > 0.001f)
        {
            twistAxis = twistAxis / len;
        }
        else
        {
            twistAxis = JPH::Vec3::sAxisX();
        }

        // Plane axis: perpendicular to twist axis
        JPH::Vec3 planeAxis;
        if (std::abs(twistAxis.Dot(JPH::Vec3::sAxisY())) < 0.99f)
        {
            planeAxis = twistAxis.Cross(JPH::Vec3::sAxisY()).Normalized();
        }
        else
        {
            planeAxis = twistAxis.Cross(JPH::Vec3::sAxisZ()).Normalized();
        }

        constraint->mTwistAxis1 = twistAxis;
        constraint->mTwistAxis2 = twistAxis;
        constraint->mPlaneAxis1 = planeAxis;
        constraint->mPlaneAxis2 = planeAxis;

        if (limits)
        {
            constraint->mNormalHalfConeAngle = limits->swingNormalHalfAngle;
            constraint->mPlaneHalfConeAngle = limits->swingPlaneHalfAngle;
            constraint->mTwistMinAngle = limits->twistMinAngle;
            constraint->mTwistMaxAngle = limits->twistMaxAngle;
            constraint->mMaxFrictionTorque = limits->maxFrictionTorque;
        }
        else
        {
            constraint->mNormalHalfConeAngle = 0.7854f;
            constraint->mPlaneHalfConeAngle = 0.7854f;
            constraint->mTwistMinAngle = -0.7854f;
            constraint->mTwistMaxAngle = 0.7854f;
            constraint->mMaxFrictionTorque = 0.0f;
        }

        constraint->mSwingType = JPH::ESwingType::Cone;

        return constraint;
    }
}
