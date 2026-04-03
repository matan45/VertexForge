#pragma once

#include "../../interfaces/destruction/IDestructionService.hpp"
#include "../../events/EventTypes.hpp"
#include <memory>

namespace services
{
    class DebrisManager;
    class DestructionEffectsManager;

    class DestructionServiceImpl : public IDestructionService
    {
    public:
        DestructionServiceImpl();
        ~DestructionServiceImpl() override;

        void registerEventHandlers() override;
        void update(float deltaTime) override;

        void applyDamage(EntityHandle entity, float amount, components::DamageType type,
                         const glm::vec3& impactPoint, const glm::vec3& impactDir) override;
        void triggerDestruction(EntityHandle entity,
                                const glm::vec3& impactPoint, const glm::vec3& impactDir, float force) override;
        float getHealth(EntityHandle entity) const override;
        bool isDestroyed(EntityHandle entity) const override;

    private:
        ::events::SubscriptionToken collisionToken{};
        std::unique_ptr<DebrisManager> debrisManager;
        std::unique_ptr<DestructionEffectsManager> effectsManager;
        uint32_t frameNumber = 0;

        void spawnFragments(EntityHandle entity,
                           const glm::vec3& impactPoint, const glm::vec3& impactDir, float force);
    };
}
