#include "DestructionServiceImpl.hpp"
#include "DebrisManager.hpp"
#include "DestructionEffectsManager.hpp"
#include "DamagePropagationManager.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/destruction/DestructionEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../events/render/MaterialEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../interfaces/physics/IPhysicsService.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../data/EditorMode.hpp"
#include "../../data/DTOs.hpp"
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <asset/AssetRef.hpp>
#include "DestructionHelpers.hpp"
#include <print/Log.hpp>
#include <algorithm>
#include <asset/AssetMetadataSerializer.hpp>

namespace services
{
    using namespace services::destruction_internal;

    DestructionServiceImpl::DestructionServiceImpl()
        : debrisManager(std::make_unique<DebrisManager>())
        , effectsManager(std::make_unique<DestructionEffectsManager>())
        , propagationManager(std::make_unique<DamagePropagationManager>())
    {
    }

    DestructionServiceImpl::~DestructionServiceImpl()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        if (collisionToken.isValid())
            dispatcher.unsubscribe(collisionToken);
        if (modeChangedToken.isValid())
            dispatcher.unsubscribe(modeChangedToken);
    }

    void DestructionServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<::events::destruction::ApplyDamageCommand>(
            [this](const ::events::destruction::ApplyDamageCommand& cmd)
            {
                applyDamage(cmd.entity, cmd.amount, cmd.damageType,
                           cmd.impactPoint, cmd.impactDirection, cmd.propagationDepth);
            });

        dispatcher.registerCommandHandler<::events::destruction::ExplosionDamageCommand>(
            [this](const ::events::destruction::ExplosionDamageCommand& cmd)
            {
                ExplosionRequest req;
                req.center = cmd.center;
                req.radius = cmd.radius;
                req.damage = cmd.damage;
                req.force = cmd.force;
                req.damageType = cmd.damageType;
                req.upwardBias = cmd.upwardBias;
                req.depth = cmd.propagationDepth;
                propagationManager->queueExplosion(req);
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

        registerCollisionHandler();

        modeChangedToken = dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& n)
            {
                if (n.currentMode == services::EditorMode::Edit)
                {
                    debrisManager->reset();
                    effectsManager->reset();
                    propagationManager->reset();
                    fragmentOffsetCache.clear();
                    frameNumber = 0;
                }
            });
    }

    void DestructionServiceImpl::registerCollisionHandler()
    {
        collisionToken = ::events::EventDispatcher::instance().subscribe<::events::physics::CollisionStartNotification>(
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
                    constexpr float damagePerDepth = 100.0f;
                    constexpr float minPenetration = 0.005f;
                    if (n.penetrationDepth < minPenetration) return;
                    float damage = n.penetrationDepth * damagePerDepth;
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

                auto checkFragment = [&](EntityHandle handle)
                {
                    if (!isValidHandle(handle, reg)) return;
                    scene::Entity e(fromHandle(handle));
                    if (e.hasComponent<components::FragmentComponent>())
                        effectsManager->onFragmentCollision(handle, n.contactPoint, n.penetrationDepth);
                };
                checkFragment(n.entityA);
                checkFragment(n.entityB);
            });
    }

    void DestructionServiceImpl::update(float deltaTime)
    {
        debrisManager->update(deltaTime, ++frameNumber);
        effectsManager->update(deltaTime);
        propagationManager->update();
    }

    void DestructionServiceImpl::applyDamage(EntityHandle entity, float amount,
                                              components::DamageType type,
                                              const glm::vec3& impactPoint,
                                              const glm::vec3& impactDir,
                                              uint32_t propagationDepth)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!isValidHandle(entity, registry)) return;

        scene::Entity sceneEntity(fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DestructibleComponent>()) return;

        auto& destructible = sceneEntity.getComponent<components::DestructibleComponent>();
        if (destructible.isDestroyed) return;

        if (destructible.damageFilter != components::DamageType::Any &&
            type != components::DamageType::Any &&
            destructible.damageFilter != type)
            return;

        // Apply damage
        destructible.currentHealth -= amount;
        destructible.currentHealth = std::max(0.0f, destructible.currentHealth);

        // Publish damage notification
        ::events::destruction::DamageAppliedNotification notification;
        notification.entity = entity;
        notification.damageAmount = amount;
        notification.remainingHealth = destructible.currentHealth;
        notification.impactPoint = impactPoint;
        notification.impactDirection = impactDir;
        notification.damageType = type;
        ::events::EventDispatcher::instance().publish(notification);

        effectsManager->onDamageApplied(entity, amount, impactPoint, impactDir, type);

        vfLogDebug("Destruction: entity {} took {:.1f} damage, health: {:.1f}/{}",
                     entity.id, amount, destructible.currentHealth, destructible.maxHealth);

        // Check if destruction threshold reached
        if (destructible.currentHealth <= destructible.destructionThreshold)
        {
            triggerDestruction(entity, impactPoint, impactDir, amount, propagationDepth);
        }
    }

    void DestructionServiceImpl::triggerDestruction(EntityHandle entity,
                                                     const glm::vec3& impactPoint,
                                                     const glm::vec3& impactDir,
                                                     float force,
                                                     uint32_t propagationDepth)
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

        vfLogInfo("Destruction: triggering destruction for entity {}", entity.id);

        spawnFragments(entity, impactPoint, impactDir, force, propagationDepth);
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

    glm::vec3 DestructionServiceImpl::computeFragmentImpulse(const glm::vec3& impactDir,
                                                               uint32_t fragmentIndex,
                                                               uint32_t fragmentCount,
                                                               float force)
    {
        float angle = static_cast<float>(fragmentIndex) * 6.283185f / static_cast<float>(fragmentCount);
        glm::vec3 spread(std::cos(angle) * 0.3f, 0.2f, std::sin(angle) * 0.3f);
        return (impactDir + spread) * force;
    }

    std::vector<FragmentSpawnRequest> DestructionServiceImpl::buildSpawnRequests(
        const components::DestructibleComponent& destructible,
        const components::TransformComponent& transform,
        const MaterialData& sourceMaterial,
        EntityHandle entity,
        const glm::vec3& fragmentDir, float force)
    {
        constexpr uint32_t maxFragments = 100;
        uint32_t fragmentCount = destructible.fragmentCount > 0
            ? std::min(maxFragments, destructible.fragmentCount)
            : 1;
        float massPerFragment = destructible.fragmentMassTotal / static_cast<float>(fragmentCount);

        std::vector<FragmentSpawnRequest> requests;
        requests.reserve(fragmentCount);

        // Load per-fragment center-of-mass offsets from .meta file (cached)
        std::string meshPath = destructible.fractureAssetRef.resolve();
        auto cacheIt = fragmentOffsetCache.find(meshPath);
        if (cacheIt == fragmentOffsetCache.end())
        {
            std::vector<glm::vec3> offsets;
            auto metaPath = asset::AssetMetadataSerializer::getMetaPath(meshPath);
            auto meta = asset::AssetMetadataSerializer::load(metaPath);
            if (meta && meta->fractureData.has_value())
            {
                const auto& frags = meta->fractureData->fragments;
                offsets.reserve(frags.size());
                for (const auto& f : frags)
                    offsets.push_back(f.centerOfMass);
            }
            cacheIt = fragmentOffsetCache.emplace(meshPath, std::move(offsets)).first;
        }
        const auto& cachedOffsets = cacheIt->second;
        std::vector<glm::vec3> fragmentOffsets(fragmentCount, glm::vec3(0.0f));
        for (uint32_t i = 0; i < fragmentCount && i < cachedOffsets.size(); ++i)
        {
            fragmentOffsets[i] = cachedOffsets[i];
        }

        glm::mat4 parentMatrix = transform.getMatrix();

        for (uint32_t i = 0; i < fragmentCount; ++i)
        {
            glm::vec3 worldPos = glm::vec3(parentMatrix * glm::vec4(fragmentOffsets[i], 1.0f));

            FragmentSpawnRequest req;
            req.position = worldPos;
            req.rotation = transform.rotation;
            req.scale = transform.scale;
            req.fractureAssetRef = destructible.fractureAssetRef;
            req.sourceMaterial = sourceMaterial;
            req.mass = massPerFragment;
            req.lifetime = destructible.fragmentLifetime;
            req.sourceEntityId = entity.id;
            req.fragmentIndex = i;
            req.collisionAudioRef = destructible.fragmentCollisionAudio;
            req.impulse = computeFragmentImpulse(fragmentDir, i, fragmentCount, force);
            requests.push_back(std::move(req));
        }
        return requests;
    }

    void DestructionServiceImpl::spawnFragments(EntityHandle entity, const glm::vec3& impactPoint,
                                                 const glm::vec3& impactDir, float force, uint32_t propagationDepth)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        if (!isValidHandle(entity, scene::EntityRegistry::getRegistry())) return;

        scene::Entity sourceEntity(fromHandle(entity));
        const auto& destructible = sourceEntity.getComponent<components::DestructibleComponent>();
        const auto& transform = sourceEntity.getComponent<components::TransformComponent>();
        if (!destructible.fractureAssetRef.isValid())
        {
            vfLogWarning("Destruction: entity {} has no fracture asset reference", entity.id);
            return;
        }
        MaterialData sourceMaterial;
        if (sourceEntity.hasComponent<components::MaterialComponent>())
        {
            const auto& mat = sourceEntity.getComponent<components::MaterialComponent>();
            sourceMaterial.defaultMaterialRef = mat.defaultMaterialRef;
            sourceMaterial.subMeshMaterials = mat.subMeshMaterials;
            sourceMaterial.parameterOverrides = mat.parameterOverrides;
        }
        glm::vec3 fragmentDir = glm::length(impactDir) > 0.001f
            ? glm::normalize(impactDir) : glm::vec3(0.0f, 1.0f, 0.0f);
        auto requests = buildSpawnRequests(destructible, transform, sourceMaterial, entity, fragmentDir, force);
        uint32_t fragmentCount = static_cast<uint32_t>(requests.size());

        // Hide original entity immediately to prevent visual overlap with fragments
        ::events::scene::SetEntityActiveCommand hideCmd;
        hideCmd.entity = entity;
        hideCmd.isActive = false;
        dispatcher.execute(hideCmd);

        debrisManager->requestSpawn(std::move(requests));
        auto effectsSnapshot = effectsManager->captureSnapshot(entity);
        if (destructible.propagationRadius > 0.0f)
        {
            PropagationRequest propReq;
            propReq.epicenter = transform.position;
            propReq.radius = destructible.propagationRadius;
            propReq.baseDamage = destructible.propagationDamage;
            propReq.damageType = components::DamageType::Explosive;
            propReq.impactDirection = impactDir;
            propReq.depth = propagationDepth;
            propagationManager->queuePropagation(propReq);
        }
        // Remove physics body before deleting entity
        ::events::physics::RemoveRigidBodyCommand removeRbCmd;
        removeRbCmd.entity = entity;
        dispatcher.execute(removeRbCmd);

        ::events::scene::DeleteEntityCommand deleteCmd;
        deleteCmd.entity = entity;
        dispatcher.execute(deleteCmd);
        effectsManager->onDestructionTriggered(effectsSnapshot, impactPoint, impactDir);
        ::events::destruction::DestructionTriggeredNotification notification;
        notification.entity = entity;
        notification.impactPoint = impactPoint;
        notification.impactDirection = impactDir;
        dispatcher.publish(notification);
        vfLogInfo("Destruction: queued {} fragments for entity {}", fragmentCount, entity.id);
    }
}
