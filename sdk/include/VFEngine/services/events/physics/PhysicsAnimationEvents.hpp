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
}
