#pragma once

#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <components/DestructionComponents.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace events::destruction {

    // ============================================
    // Commands
    // ============================================

    struct ApplyDamageCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        float amount = 0.0f;
        components::DamageType damageType = components::DamageType::Any;
        glm::vec3 impactPoint{0.0f};
        glm::vec3 impactDirection{0.0f};

        std::string_view getName() const override { return "ApplyDamage"; }
    };

    struct TriggerDestructionCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::vec3 impactPoint{0.0f};
        glm::vec3 impactDirection{0.0f};
        float force = 10.0f;

        std::string_view getName() const override { return "TriggerDestruction"; }
    };

    // ============================================
    // Queries
    // ============================================

    struct GetHealthQuery : ::events::IQuery<float> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetHealth"; }
    };

    struct IsDestroyedQuery : ::events::IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsDestroyed"; }
    };

    // ============================================
    // Notifications
    // ============================================

    struct DamageAppliedNotification : ::events::INotification {
        services::EntityHandle entity;
        float damageAmount = 0.0f;
        float remainingHealth = 0.0f;
        glm::vec3 impactPoint{0.0f};
        glm::vec3 impactDirection{0.0f};
        components::DamageType damageType = components::DamageType::Any;

        std::string_view getName() const override { return "DamageApplied"; }
    };

    struct DestructionTriggeredNotification : ::events::INotification {
        services::EntityHandle entity;
        glm::vec3 impactPoint{0.0f};
        glm::vec3 impactDirection{0.0f};
        std::vector<services::EntityHandle> fragmentEntities;

        std::string_view getName() const override { return "DestructionTriggered"; }
    };

    struct FragmentDetachedNotification : ::events::INotification {
        services::EntityHandle sourceEntity;
        services::EntityHandle fragmentEntity;
        uint32_t fragmentIndex = 0;

        std::string_view getName() const override { return "FragmentDetached"; }
    };
}
