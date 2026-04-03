#pragma once

#include "../../interfaces/destruction/IDestructionService.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include <unordered_set>

namespace services
{
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
        events::SubscriptionToken collisionToken{};

        void onCollisionStart(const events::physics::CollisionStartNotification& notification);
        void spawnFragments(EntityHandle entity,
                           const glm::vec3& impactPoint, const glm::vec3& impactDir, float force);
        void cleanupExpiredFragments(float deltaTime);
    };
}
