#pragma once

#include "../../data/EntityHandle.hpp"
#include <components/DestructionComponents.hpp>
#include <glm/glm.hpp>
#include <deque>
#include <cstdint>

namespace services
{
    struct PropagationConfig
    {
        uint32_t maxPropagationDepth = 3;
        uint32_t maxDamagesPerFrame = 4;
        uint32_t maxQueueSize = 64;
    };

    struct PropagationRequest
    {
        glm::vec3 epicenter{0.0f};
        float radius = 5.0f;
        float baseDamage = 50.0f;
        components::DamageType damageType = components::DamageType::Explosive;
        glm::vec3 impactDirection{0.0f};
        uint32_t depth = 0;
    };

    struct ExplosionRequest
    {
        glm::vec3 center{0.0f};
        float radius = 5.0f;
        float damage = 100.0f;
        float force = 20.0f;
        components::DamageType damageType = components::DamageType::Explosive;
        float upwardBias = 0.3f;
        uint32_t depth = 0;
    };

    class DamagePropagationManager
    {
    public:
        explicit DamagePropagationManager(PropagationConfig config = {});

        void queuePropagation(const PropagationRequest& request);
        void queueExplosion(const ExplosionRequest& request);
        void update();
        void reset();

    private:
        PropagationConfig config;
        std::deque<PropagationRequest> propagationQueue;
        std::deque<ExplosionRequest> explosionQueue;

        void applyRadialDamage(const glm::vec3& epicenter, float radius, float baseDamage,
                               components::DamageType damageType, const glm::vec3& impactDir,
                               uint32_t depth);
        void applyExplosionForces(const glm::vec3& center, float radius, float force,
                                  float upwardBias);
    };
}
