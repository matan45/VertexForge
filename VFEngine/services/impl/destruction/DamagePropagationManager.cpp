#include "DamagePropagationManager.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/destruction/DestructionEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include <scene/EntityRegistry.hpp>
#include <scene/Entity.hpp>
#include <components/Components.hpp>
#include "DestructionHelpers.hpp"
#include <print/Log.hpp>
#include <algorithm>

namespace services
{
    using namespace services::destruction_internal;

    DamagePropagationManager::DamagePropagationManager(PropagationConfig config)
        : config(config)
    {
    }

    void DamagePropagationManager::queuePropagation(const PropagationRequest& request)
    {
        if (request.depth >= config.maxPropagationDepth)
        {
            return;
        }

        if (propagationQueue.size() < config.maxQueueSize)
        {
            propagationQueue.push_back(request);
        }
    }

    void DamagePropagationManager::queueExplosion(const ExplosionRequest& request)
    {
        if (explosionQueue.size() < config.maxQueueSize)
        {
            explosionQueue.push_back(request);
        }
    }

    void DamagePropagationManager::update()
    {
        uint32_t processed = 0;

        while (!propagationQueue.empty() && processed < config.maxDamagesPerFrame)
        {
            auto req = std::move(propagationQueue.front());
            propagationQueue.pop_front();

            applyRadialDamage(req);
            ++processed;
        }

        while (!explosionQueue.empty() && processed < config.maxDamagesPerFrame)
        {
            auto req = std::move(explosionQueue.front());
            explosionQueue.pop_front();

            PropagationRequest propReq;
            propReq.epicenter = req.center;
            propReq.radius = req.radius;
            propReq.baseDamage = req.damage;
            propReq.damageType = req.damageType;
            propReq.impactDirection = glm::vec3(0.0f, 1.0f, 0.0f);
            propReq.depth = req.depth;
            applyRadialDamage(propReq);
            applyExplosionForces(req.center, req.radius, req.force, req.upwardBias);

            // Publish explosion notification
            ::events::destruction::ExplosionOccurredNotification notification;
            notification.center = req.center;
            notification.radius = req.radius;
            notification.damage = req.damage;
            notification.force = req.force;
            ::events::EventDispatcher::instance().publish(notification);

            ++processed;
        }
    }

    void DamagePropagationManager::reset()
    {
        propagationQueue.clear();
        explosionQueue.clear();
    }

    void DamagePropagationManager::applyRadialDamage(const PropagationRequest& request)
    {
        if (request.depth + 1 > config.maxPropagationDepth)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto view = registry.view<components::DestructibleComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            const auto& destructible = view.get<components::DestructibleComponent>(entity);
            if (destructible.isDestroyed)
            {
                continue;
            }

            const auto& transform = view.get<components::TransformComponent>(entity);
            float dist = glm::distance(transform.position, request.epicenter);

            if (dist > request.radius || dist < 0.001f)
            {
                continue;
            }

            float falloff = 1.0f - (dist / request.radius);
            float actualDamage = request.baseDamage * falloff;
            if (actualDamage < 0.1f)
            {
                continue;
            }

            glm::vec3 direction = glm::normalize(transform.position - request.epicenter);

            EntityHandle handle;
            handle.id = static_cast<uint64_t>(static_cast<uint32_t>(entity));

            ::events::destruction::ApplyDamageCommand cmd;
            cmd.entity = handle;
            cmd.amount = actualDamage;
            cmd.damageType = request.damageType;
            cmd.impactPoint = transform.position;
            cmd.impactDirection = direction;
            cmd.propagationDepth = request.depth + 1;
            dispatcher.execute(cmd);
        }

        vfLogDebug("Propagation: radial damage at ({:.1f},{:.1f},{:.1f}) radius={:.1f} depth={}",
                     request.epicenter.x, request.epicenter.y, request.epicenter.z,
                     request.radius, request.depth);
    }

    void DamagePropagationManager::applyExplosionForces(const glm::vec3& center, float radius,
                                                         float force, float upwardBias)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto view = registry.view<components::TransformComponent, components::RigidBodyComponent>();
        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
            float dist = glm::distance(transform.position, center);

            if (dist > radius || dist < 0.001f)
            {
                continue;
            }

            float falloff = 1.0f - (dist / radius);
            glm::vec3 direction = glm::normalize(transform.position - center);

            glm::vec3 impulse = direction * force * falloff;
            impulse.y += force * upwardBias * falloff;

            EntityHandle handle;
            handle.id = static_cast<uint64_t>(static_cast<uint32_t>(entity));

            ::events::physics::ApplyImpulseCommand impulseCmd;
            impulseCmd.entity = handle;
            impulseCmd.impulse = impulse;
            dispatcher.execute(impulseCmd);
        }
    }
}
