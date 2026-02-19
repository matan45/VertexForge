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
        Ragdoll = 2
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

    struct PhysicsAnimationConfig
    {
        PhysicsAnimationMode defaultMode = PhysicsAnimationMode::Animated;
        uint8_t collisionLayer = 1;
        std::vector<BoneBodyMapping> boneBodyMappings;
        std::vector<JointConstraintLimits> jointLimits;
        float kinematicToRagdollBlendTime = 0.2f;

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
    };
}
