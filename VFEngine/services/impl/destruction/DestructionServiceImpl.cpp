#include "DestructionServiceImpl.hpp"
#include "DebrisManager.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/destruction/DestructionEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../events/render/MaterialEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../interfaces/physics/IPhysicsService.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"
#include "../../data/DTOs.hpp"
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace services
{
    namespace
    {
        // Convert EntityHandle to entt::entity
        entt::entity fromHandle(EntityHandle handle)
        {
            return static_cast<entt::entity>(static_cast<uint32_t>(handle.id));
        }

        bool isValidHandle(EntityHandle handle, entt::registry& registry)
        {
            if (!handle.isValid()) return false;
            auto entity = fromHandle(handle);
            return registry.valid(entity);
        }
    }

    DestructionServiceImpl::DestructionServiceImpl()
        : debrisManager(std::make_unique<DebrisManager>())
    {
    }

    DestructionServiceImpl::~DestructionServiceImpl()
    {
        if (collisionToken.isValid())
        {
            ::events::EventDispatcher::instance().unsubscribe(collisionToken);
        }
    }

    void DestructionServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<::events::destruction::ApplyDamageCommand>(
            [this](const ::events::destruction::ApplyDamageCommand& cmd)
            {
                applyDamage(cmd.entity, cmd.amount, cmd.damageType,
                           cmd.impactPoint, cmd.impactDirection);
            });

        dispatcher.registerCommandHandler<::events::destruction::TriggerDestructionCommand>(
            [this](const ::events::destruction::TriggerDestructionCommand& cmd)
            {
                triggerDestruction(cmd.entity, cmd.impactPoint, cmd.impactDirection, cmd.force);
            });

        dispatcher.registerQueryHandler<::events::destruction::GetHealthQuery>(
            [this](const ::events::destruction::GetHealthQuery& query)
            {
                return getHealth(query.entity);
            });

        dispatcher.registerQueryHandler<::events::destruction::IsDestroyedQuery>(
            [this](const ::events::destruction::IsDestroyedQuery& query)
            {
                return isDestroyed(query.entity);
            });

        collisionToken = dispatcher.subscribe<::events::physics::CollisionStartNotification>(
            [this](const ::events::physics::CollisionStartNotification& n)
            {
                auto& reg = scene::EntityRegistry::getRegistry();
                auto checkEntity = [&](EntityHandle handle, const glm::vec3& normal)
                {
                    if (!isValidHandle(handle, reg)) return;
                    scene::Entity e(fromHandle(handle));
                    if (!e.hasComponent<components::DestructibleComponent>()) return;
                    auto& d = e.getComponent<components::DestructibleComponent>();
                    if (d.isDestroyed) return;
                    float damage = n.penetrationDepth * 100.0f;
                    if (damage > d.destructionThreshold * 0.1f)
                    {
                        ::events::destruction::ApplyDamageCommand cmd;
                        cmd.entity = handle;
                        cmd.amount = damage;
                        cmd.damageType = components::DamageType::Any;
                        cmd.impactPoint = n.contactPoint;
                        cmd.impactDirection = normal;
                        ::events::EventDispatcher::instance().execute(cmd);
                    }
                };
                checkEntity(n.entityA, n.normal);
                checkEntity(n.entityB, -n.normal);
            });
    }

    void DestructionServiceImpl::update(float deltaTime)
    {
        debrisManager->update(deltaTime, ++frameNumber);
    }

    void DestructionServiceImpl::applyDamage(EntityHandle entity, float amount,
                                              components::DamageType type,
                                              const glm::vec3& impactPoint,
                                              const glm::vec3& impactDir)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!isValidHandle(entity, registry))
        {
            return;
        }

        scene::Entity sceneEntity(fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DestructibleComponent>())
        {
            return;
        }

        auto& destructible = sceneEntity.getComponent<components::DestructibleComponent>();

        if (destructible.isDestroyed)
        {
            return;
        }

        // Check damage type filter
        if (destructible.damageFilter != components::DamageType::Any &&
            destructible.damageFilter != type)
        {
            return;
        }

        // Apply damage
        destructible.currentHealth -= amount;
        destructible.currentHealth = std::max(0.0f, destructible.currentHealth);

        // Publish damage notification
        ::events::destruction::DamageAppliedNotification notification;
        notification.entity = entity;
        notification.damageAmount = amount;
        notification.remainingHealth = destructible.currentHealth;
        ::events::EventDispatcher::instance().publish(notification);

        spdlog::debug("Destruction: entity {} took {:.1f} damage, health: {:.1f}/{}",
                     entity.id, amount, destructible.currentHealth, destructible.maxHealth);

        // Check if destruction threshold reached
        if (destructible.currentHealth <= destructible.destructionThreshold)
        {
            triggerDestruction(entity, impactPoint, impactDir, amount);
        }
    }

    void DestructionServiceImpl::triggerDestruction(EntityHandle entity,
                                                     const glm::vec3& impactPoint,
                                                     const glm::vec3& impactDir,
                                                     float force)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!isValidHandle(entity, registry))
        {
            return;
        }

        scene::Entity sceneEntity(fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DestructibleComponent>())
        {
            return;
        }

        auto& destructible = sceneEntity.getComponent<components::DestructibleComponent>();
        if (destructible.isDestroyed)
        {
            return;
        }

        destructible.isDestroyed = true;

        spdlog::info("Destruction: triggering destruction for entity {}", entity.id);

        spawnFragments(entity, impactPoint, impactDir, force);
    }

    float DestructionServiceImpl::getHealth(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!isValidHandle(entity, registry))
        {
            return 0.0f;
        }

        scene::Entity sceneEntity(fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DestructibleComponent>())
        {
            return 0.0f;
        }

        return sceneEntity.getComponent<components::DestructibleComponent>().currentHealth;
    }

    bool DestructionServiceImpl::isDestroyed(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DestructibleComponent>())
        {
            return false;
        }

        return sceneEntity.getComponent<components::DestructibleComponent>().isDestroyed;
    }

    void DestructionServiceImpl::spawnFragments(EntityHandle entity,
                                                 const glm::vec3& impactPoint,
                                                 const glm::vec3& impactDir,
                                                 float force)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!isValidHandle(entity, registry))
        {
            return;
        }

        scene::Entity sourceEntity(fromHandle(entity));
        const auto& destructible = sourceEntity.getComponent<components::DestructibleComponent>();
        const auto& transform = sourceEntity.getComponent<components::TransformComponent>();

        // Get material from source entity (if present)
        MaterialData sourceMaterial;
        if (sourceEntity.hasComponent<components::MaterialComponent>())
        {
            const auto& mat = sourceEntity.getComponent<components::MaterialComponent>();
            sourceMaterial.defaultMaterialRef = mat.defaultMaterialRef;
            sourceMaterial.subMeshMaterials = mat.subMeshMaterials;
            sourceMaterial.parameterOverrides = mat.parameterOverrides;
        }

        // Get the fracture asset reference
        const auto& fractureRef = destructible.fractureAssetRef;
        if (!fractureRef.isValid())
        {
            spdlog::warn("Destruction: entity {} has no fracture asset reference", entity.id);
            return;
        }

        constexpr uint32_t maxFragments = 100;
        uint32_t fragmentCount = std::min(maxFragments, static_cast<uint32_t>(10));

        glm::vec3 sourcePos = transform.position;
        float totalMass = destructible.fragmentMassTotal;
        float massPerFragment = totalMass / static_cast<float>(fragmentCount);
        float lifetime = destructible.fragmentLifetime;

        // Build spawn requests for DebrisManager (batched over multiple frames)
        glm::vec3 fragmentDir = impactDir;
        if (glm::length(fragmentDir) > 0.001f)
        {
            fragmentDir = glm::normalize(fragmentDir);
        }
        else
        {
            fragmentDir = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        std::vector<FragmentSpawnRequest> requests;
        requests.reserve(fragmentCount);

        for (uint32_t i = 0; i < fragmentCount; ++i)
        {
            FragmentSpawnRequest req;
            req.position = sourcePos;
            req.rotation = transform.rotation;
            req.scale = transform.scale;
            req.fractureAssetRef = fractureRef;
            req.sourceMaterial = sourceMaterial;
            req.mass = massPerFragment;
            req.lifetime = lifetime;
            req.sourceEntityId = entity.id;
            req.fragmentIndex = i;

            float angle = static_cast<float>(i) * 6.283185f / static_cast<float>(fragmentCount);
            glm::vec3 spread(std::cos(angle) * 0.3f, 0.2f, std::sin(angle) * 0.3f);
            req.impulse = (fragmentDir + spread) * force;

            requests.push_back(std::move(req));
        }

        debrisManager->requestSpawn(std::move(requests));

        // Delete the original entity
        ::events::scene::DeleteEntityCommand deleteCmd;
        deleteCmd.entity = entity;
        dispatcher.execute(deleteCmd);

        // Publish destruction notification
        ::events::destruction::DestructionTriggeredNotification notification;
        notification.entity = entity;
        notification.impactPoint = impactPoint;
        notification.impactDirection = impactDir;
        dispatcher.publish(notification);

        spdlog::info("Destruction: queued {} fragments for entity {}", fragmentCount, entity.id);
    }
}
