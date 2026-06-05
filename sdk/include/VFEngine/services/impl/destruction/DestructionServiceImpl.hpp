#pragma once

#include "../../interfaces/destruction/IDestructionService.hpp"
#include "../../events/EventTypes.hpp"
#include "DebrisManager.hpp"
#include <components/CoreComponents.hpp>
#include <asset/AssetMetadata.hpp>
#include <memory>
#include <vector>
#include <unordered_map>

namespace services
{
    class DebrisManager;
    class DestructionEffectsManager;
    class DamagePropagationManager;

    class DestructionServiceImpl : public IDestructionService
    {
    public:
        DestructionServiceImpl();
        ~DestructionServiceImpl() override;

        void registerEventHandlers() override;
        void update(float deltaTime) override;

        void applyDamage(EntityHandle entity, float amount, components::DamageType type,
                         const glm::vec3& impactPoint, const glm::vec3& impactDir,
                         uint32_t propagationDepth = 0) override;
        void triggerDestruction(EntityHandle entity,
                                const glm::vec3& impactPoint, const glm::vec3& impactDir, float force,
                                uint32_t propagationDepth = 0) override;
        float getHealth(EntityHandle entity) const override;
        bool isDestroyed(EntityHandle entity) const override;

    private:
        ::events::SubscriptionToken collisionToken{};
        ::events::SubscriptionToken modeChangedToken{};
        std::unique_ptr<DebrisManager> debrisManager;
        std::unique_ptr<DestructionEffectsManager> effectsManager;
        std::unique_ptr<DamagePropagationManager> propagationManager;
        uint32_t frameNumber = 0;

        void registerCollisionHandler();

        void spawnFragments(EntityHandle entity,
                           const glm::vec3& impactPoint, const glm::vec3& impactDir, float force,
                           uint32_t propagationDepth = 0);
        std::vector<FragmentSpawnRequest> buildSpawnRequests(
            const components::DestructibleComponent& destructible,
            const components::TransformComponent& transform,
            const MaterialData& sourceMaterial,
            EntityHandle entity,
            const glm::vec3& fragmentDir, float force);
        static glm::vec3 computeFragmentImpulse(const glm::vec3& impactDir,
                                                  uint32_t fragmentIndex, uint32_t fragmentCount,
                                                  float force);

        std::unordered_map<std::string, std::vector<glm::vec3>> fragmentOffsetCache;
    };
}
