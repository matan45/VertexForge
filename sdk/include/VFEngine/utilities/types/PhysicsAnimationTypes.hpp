#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include "PhysicsTypes.hpp"

namespace types
{
    enum class PhysicsAnimationMode : uint8_t
    {
        Animated = 0,
        Kinematic = 1,
        Ragdoll = 2,
        PoweredRagdoll = 3
    };

    struct BoneBodyMapping
    {
        std::string boneName;
        ColliderShape shape = ColliderShape::Capsule;
        glm::vec3 size{0.1f, 0.3f, 0.1f};
        glm::vec3 offset{0.0f};
        glm::quat rotationOffset{1.0f, 0.0f, 0.0f, 0.0f};
        float mass = 1.0f;
        float friction = 0.5f;
        float restitution = 0.0f;
        uint8_t collisionLayer = 255; // 255 = use global config layer
    };

    struct JointConstraintLimits
    {
        std::string boneName;
        float swingNormalHalfAngle = 0.7854f;
        float swingPlaneHalfAngle = 0.7854f;
        float twistMinAngle = -0.7854f;
        float twistMaxAngle = 0.7854f;
        float maxFrictionTorque = 0.0f;
    };

    struct BoneMotorSettings
    {
        std::string boneName;
        float strength = 1.0f;   // 0 = pure ragdoll joint, 1 = full pose tracking
        float frequency = 20.0f; // motor spring frequency in Hz (build-time)
        float damping = 1.0f;    // motor spring damping ratio (build-time)
        float maxTorque = 150.0f; // N*m available at strength 1.0
    };

    struct HitReactionSettings
    {
        float defaultRecoverTime = 0.6f;
        float strengthDip = 0.0f;  // strength the affected chain drops to on hit
        int chainDepth = -1;       // -1 = all descendants of the hit bone
        float chainFalloff = 1.0f; // per-level multiplier applied to the dip amount
    };

    struct PhysicsAnimationConfig
    {
        PhysicsAnimationMode defaultMode = PhysicsAnimationMode::Animated;
        uint8_t collisionLayer = 1;
        std::vector<BoneBodyMapping> boneBodyMappings;
        std::vector<JointConstraintLimits> jointLimits;
        float kinematicToRagdollBlendTime = 0.2f;

        // Powered ragdoll motors
        std::vector<BoneMotorSettings> boneMotors; // per-bone overrides; defaults below apply otherwise
        float defaultMotorStrength = 1.0f;
        float defaultMotorFrequency = 20.0f;
        float defaultMotorDamping = 1.0f;
        float defaultMotorMaxTorque = 150.0f;
        float rootMotorStrength = 1.0f; // velocity-drive of the root body toward the entity transform
        float poweredBlendInTime = 0.15f;
        float ragdollToAnimatedBlendTime = 0.3f;
        HitReactionSettings hitReaction;
        float settleLinearVelocityThreshold = 0.05f; // m/s
        float settleAngularVelocityThreshold = 0.2f; // rad/s
        int settleFrameCount = 30;

        const BoneBodyMapping* findMapping(const std::string& boneName) const
        {
            for (const auto& m : boneBodyMappings)
            {
                if (m.boneName == boneName) return &m;
            }
            return nullptr;
        }

        const JointConstraintLimits* findJointLimits(const std::string& boneName) const
        {
            for (const auto& jl : jointLimits)
            {
                if (jl.boneName == boneName) return &jl;
            }
            return nullptr;
        }

        const BoneMotorSettings* findBoneMotor(const std::string& boneName) const
        {
            for (const auto& bm : boneMotors)
            {
                if (bm.boneName == boneName) return &bm;
            }
            return nullptr;
        }
    };
}
