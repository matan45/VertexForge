#include "DestructionServiceImpl.hpp"
#include "../../events/destruction/DestructionEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../events/render/MaterialEvents.hpp"
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

    DestructionServiceImpl::DestructionServiceImpl() = default;

    DestructionServiceImpl::~DestructionServiceImpl()
    {
        if (collisionToken != 0)
        {
            events::EventDispatcher::instance().unsubscribe(collisionToken);
        }
    }

    void DestructionServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::destruction::ApplyDamageCommand>(
            [this](const events::destruction::ApplyDamageCommand& cmd)
            {
                applyDamage(cmd.entity, cmd.amount, cmd.damageType,
                           cmd.impactPoint, cmd.impactDirection);
            });

        dispatcher.registerCommandHandler<events::destruction::TriggerDestructionCommand>(
            [this](const events::destruction::TriggerDestructionCommand& cmd)
            {
                triggerDestruction(cmd.entity, cmd.impactPoint, cmd.impactDirection, cmd.force);
            });

        dispatcher.registerQueryHandler<events::destruction::GetHealthQuery>(
            [this](const events::destruction::GetHealthQuery& query)
            {
                return getHealth(query.entity);
            });

        dispatcher.registerQueryHandler<events::destruction::IsDestroyedQuery>(
            [this](const events::destruction::IsDestroyedQuery& query)
            {
                return isDestroyed(query.entity);
            });

        collisionToken = dispatcher.subscribe<events::physics::CollisionStartNotification>(
            [this](const events::physics::CollisionStartNotification& notification)
            {
                onCollisionStart(notification);
            });
    }

    void DestructionServiceImpl::update(float deltaTime)
    {
        cleanupExpiredFragments(deltaTime);
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
        events::destruction::DamageAppliedNotification notification;
        notification.entity = entity;
        notification.damageAmount = amount;
        notification.remainingHealth = destructible.currentHealth;
        events::EventDispatcher::instance().publish(notification);

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

    void DestructionServiceImpl::onCollisionStart(
        const events::physics::CollisionStartNotification& notification)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Check entity A
        if (isValidHandle(notification.entityA, registry))
        {
            scene::Entity entityA(fromHandle(notification.entityA));
            if (entityA.hasComponent<components::DestructibleComponent>())
            {
                auto& destructible = entityA.getComponent<components::DestructibleComponent>();
                if (!destructible.isDestroyed)
                {
                    // Estimate damage from penetration depth (simple model)
                    float damage = notification.penetrationDepth * 100.0f;
                    if (damage > destructible.destructionThreshold * 0.1f)
                    {
                        events::destruction::ApplyDamageCommand cmd;
                        cmd.entity = notification.entityA;
                        cmd.amount = damage;
                        cmd.damageType = components::DamageType::Any;
                        cmd.impactPoint = notification.contactPoint;
                        cmd.impactDirection = notification.normal;
                        events::EventDispatcher::instance().execute(cmd);
                    }
                }
            }
        }

        // Check entity B
        if (isValidHandle(notification.entityB, registry))
        {
            scene::Entity entityB(fromHandle(notification.entityB));
            if (entityB.hasComponent<components::DestructibleComponent>())
            {
                auto& destructible = entityB.getComponent<components::DestructibleComponent>();
                if (!destructible.isDestroyed)
                {
                    float damage = notification.penetrationDepth * 100.0f;
                    if (damage > destructible.destructionThreshold * 0.1f)
                    {
                        events::destruction::ApplyDamageCommand cmd;
                        cmd.entity = notification.entityB;
                        cmd.amount = damage;
                        cmd.damageType = components::DamageType::Any;
                        cmd.impactPoint = notification.contactPoint;
                        cmd.impactDirection = -notification.normal;
                        events::EventDispatcher::instance().execute(cmd);
                    }
                }
            }
        }
    }

    void DestructionServiceImpl::spawnFragments(EntityHandle entity,
                                                 const glm::vec3& impactPoint,
                                                 const glm::vec3& impactDir,
                                                 float force)
    {
        auto& dispatcher = events::EventDispatcher::instance();
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
        if (fractureRef.guid.empty())
        {
            spdlog::warn("Destruction: entity {} has no fracture asset reference", entity.id);
            return;
        }

        // For now, determine fragment count from the fracture mesh sub-mesh count
        // The fracture .vfMesh stores each fragment as a separate sub-mesh
        // We use a reasonable default until the mesh is loaded
        constexpr uint32_t maxFragments = 100;
        uint32_t fragmentCount = std::min(maxFragments, static_cast<uint32_t>(10));

        glm::vec3 sourcePos = transform.position;
        float totalMass = destructible.fragmentMassTotal;
        float massPerFragment = totalMass / static_cast<float>(fragmentCount);
        float lifetime = destructible.fragmentLifetime;

        std::vector<EntityHandle> fragmentEntities;
        fragmentEntities.reserve(fragmentCount);

        for (uint32_t i = 0; i < fragmentCount; ++i)
        {
            // Create fragment entity
            events::scene::CreateEntityCommand createCmd;
            createCmd.name = "fragment_" + std::to_string(i);
            auto fragmentHandle = dispatcher.execute(createCmd);

            if (!fragmentHandle.isValid())
            {
                continue;
            }

            // Set transform (offset from source position)
            events::scene::SetTransformCommand transformCmd;
            transformCmd.entity = fragmentHandle;
            transformCmd.transform.position = sourcePos;
            transformCmd.transform.rotation = transform.rotation;
            transformCmd.transform.scale = transform.scale;
            dispatcher.execute(transformCmd);

            // Set mesh component pointing to fracture asset
            events::scene::AddMeshComponentCommand meshCmd;
            meshCmd.entity = fragmentHandle;
            dispatcher.execute(meshCmd);

            events::scene::SetMeshDataCommand meshDataCmd;
            meshDataCmd.entity = fragmentHandle;
            meshDataCmd.meshData.meshRef = fractureRef;
            dispatcher.execute(meshDataCmd);

            // Copy material from source
            events::render::SetMaterialDataCommand matCmd;
            matCmd.entity = fragmentHandle;
            matCmd.materialData = sourceMaterial;
            dispatcher.execute(matCmd);

            // Add fragment component for lifetime tracking
            scene::Entity fragEntity(fromHandle(fragmentHandle));
            auto& fragComp = fragEntity.addComponent<components::FragmentComponent>();
            fragComp.sourceEntityId = entity.id;
            fragComp.fragmentIndex = i;
            fragComp.lifetime = lifetime;
            fragComp.elapsed = 0.0f;

            // Add physics: rigid body + collider
            events::physics::AddRigidBodyCommand rbCmd;
            rbCmd.entity = fragmentHandle;
            rbCmd.rigidBody.type = types::RigidBodyType::Dynamic;
            rbCmd.rigidBody.mass = massPerFragment;
            rbCmd.rigidBody.linearDamping = 0.5f;
            rbCmd.rigidBody.angularDamping = 0.5f;
            rbCmd.rigidBody.activateOnAdd = true;
            rbCmd.collider.shape = types::ColliderShape::ConvexMesh;
            rbCmd.collider.meshPath = fractureRef.path;
            rbCmd.collider.collisionLayer = 1;
            dispatcher.execute(rbCmd);

            // Apply impulse away from impact point
            glm::vec3 fragmentDir = impactDir;
            if (glm::length(fragmentDir) > 0.001f)
            {
                fragmentDir = glm::normalize(fragmentDir);
            }
            else
            {
                fragmentDir = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            // Add some spread based on fragment index
            float angle = static_cast<float>(i) * 6.283185f / static_cast<float>(fragmentCount);
            glm::vec3 spread(std::cos(angle) * 0.3f, 0.2f, std::sin(angle) * 0.3f);
            glm::vec3 impulse = (fragmentDir + spread) * force;

            events::physics::ApplyImpulseCommand impulseCmd;
            impulseCmd.entity = fragmentHandle;
            impulseCmd.impulse = impulse;
            dispatcher.execute(impulseCmd);

            fragmentEntities.push_back(fragmentHandle);
        }

        // Delete the original entity
        events::scene::DeleteEntityCommand deleteCmd;
        deleteCmd.entity = entity;
        dispatcher.execute(deleteCmd);

        // Publish destruction notification
        events::destruction::DestructionTriggeredNotification notification;
        notification.entity = entity;
        notification.impactPoint = impactPoint;
        notification.impactDirection = impactDir;
        notification.fragmentEntities = std::move(fragmentEntities);
        dispatcher.publish(notification);

        spdlog::info("Destruction: spawned {} fragments for entity {}",
                    notification.fragmentEntities.size(), entity.id);
    }

    void DestructionServiceImpl::cleanupExpiredFragments(float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = events::EventDispatcher::instance();

        std::vector<entt::entity> toDelete;

        auto view = registry.view<components::FragmentComponent>();
        for (auto entity : view)
        {
            auto& fragment = view.get<components::FragmentComponent>(entity);
            fragment.elapsed += deltaTime;

            if (fragment.elapsed >= fragment.lifetime)
            {
                toDelete.push_back(entity);
            }
        }

        for (auto entity : toDelete)
        {
            EntityHandle handle;
            handle.id = static_cast<uint64_t>(static_cast<uint32_t>(entity));

            events::scene::DeleteEntityCommand deleteCmd;
            deleteCmd.entity = handle;
            dispatcher.execute(deleteCmd);
        }
    }
}
