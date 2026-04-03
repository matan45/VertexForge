#pragma once

#include "../../data/EntityHandle.hpp"
#include <components/DestructionComponents.hpp>
#include <glm/glm.hpp>

namespace services
{
    class IDestructionService
    {
    public:
        virtual ~IDestructionService() = default;

        virtual void registerEventHandlers() = 0;
        virtual void update(float deltaTime) = 0;

        virtual void applyDamage(EntityHandle entity, float amount, components::DamageType type,
                                 const glm::vec3& impactPoint, const glm::vec3& impactDir,
                                 uint32_t propagationDepth = 0) = 0;
        virtual void triggerDestruction(EntityHandle entity,
                                        const glm::vec3& impactPoint, const glm::vec3& impactDir, float force,
                                        uint32_t propagationDepth = 0) = 0;
        virtual float getHealth(EntityHandle entity) const = 0;
        virtual bool isDestroyed(EntityHandle entity) const = 0;
    };
}
