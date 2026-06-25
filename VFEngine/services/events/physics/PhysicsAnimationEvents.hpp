#pragma once

#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include <glm/glm.hpp>

namespace events::physicsAnimation
{
    // ============================================
    // COMMANDS
    // ============================================

    struct ActivateRagdollCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        glm::vec3 impulse{0.0f};
        int impulseAnimBoneIndex = -1; // -1 = all bodies, >= 0 = specific bone
        std::string_view getName() const override { return "ActivateRagdoll"; }
    };

    struct DeactivateRagdollCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "DeactivateRagdoll"; }
    };

    struct SetKinematicBonesEnabledCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        bool enabled = true;
        std::string_view getName() const override { return "SetKinematicBonesEnabled"; }
    };

    struct ApplyRagdollImpulseCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        glm::vec3 impulse{0.0f};
        std::string_view getName() const override { return "ApplyRagdollImpulse"; }
    };

    struct ApplyRagdollBoneImpulseCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        int animBoneIndex = -1;
        glm::vec3 impulse{0.0f};
        std::string_view getName() const override { return "ApplyRagdollBoneImpulse"; }
    };

    struct SetPhysicsAnimationModeCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        types::PhysicsAnimationMode mode = types::PhysicsAnimationMode::Animated;
        std::string_view getName() const override { return "SetPhysicsAnimationMode"; }
    };

    struct SetBoneMotorStrengthCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        std::string boneName;
        float strength = 1.0f;
        std::string_view getName() const override { return "SetBoneMotorStrength"; }
    };

    struct SetGlobalMotorStrengthCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        float strength = 1.0f;
        std::string_view getName() const override { return "SetGlobalMotorStrength"; }
    };

    struct HitReactionCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        std::string boneName;
        glm::vec3 impulse{0.0f};
        float recoverTime = -1.0f; // < 0 = use config default
        std::string_view getName() const override { return "HitReaction"; }
    };

    // VK-1437: tear down provider-side physics-animation state (ragdoll + kinematic bones) for an
    // entity without removing the ECS component. Idempotent (no-ops if no state exists). Executed when
    // the PhysicsAnimationComponent is removed at runtime so Jolt bodies don't leak.
    struct DestroyPhysicsAnimationCommand : ::events::ICommand<void>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "DestroyPhysicsAnimation"; }
    };

    // ============================================
    // QUERIES
    // ============================================

    struct IsRagdollActiveQuery : ::events::IQuery<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "IsRagdollActive"; }
    };

    struct GetPhysicsAnimationModeQuery : ::events::IQuery<types::PhysicsAnimationMode>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetPhysicsAnimationMode"; }
    };

    struct HasPhysicsAnimationQuery : ::events::IQuery<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "HasPhysicsAnimation"; }
    };

    struct IsRagdollSettledQuery : ::events::IQuery<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "IsRagdollSettled"; }
    };

    // ============================================
    // NOTIFICATIONS
    // ============================================

    struct RagdollActivatedNotification : ::events::INotification
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RagdollActivated"; }
    };

    struct RagdollDeactivatedNotification : ::events::INotification
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RagdollDeactivated"; }
    };

    struct RagdollSettledNotification : ::events::INotification
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RagdollSettled"; }
    };
}
