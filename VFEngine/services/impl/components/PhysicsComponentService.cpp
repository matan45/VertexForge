#include "PhysicsComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"

namespace services
{
    PhysicsComponentService::PhysicsComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : sceneGraph(std::move(sceneGraph))
    {
    }

    // ========== COLLIDER COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addColliderComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ColliderComponent>())
        {
            sceneEntity.addComponent<components::ColliderComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeColliderComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::ColliderComponent>())
        {
            sceneEntity.removeComponent<components::ColliderComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasColliderComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::ColliderComponent>();
    }

    std::optional<ColliderComponentData> PhysicsComponentService::getColliderData(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ColliderComponent>())
        {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::ColliderComponent>();
        ColliderComponentData data;
        data.shape = comp.shape;
        data.size = comp.size;
        data.height = comp.height;
        data.offset = comp.offset;
        data.meshRef = comp.meshRef;
        data.isTrigger = comp.isTrigger;
        data.collisionLayer = comp.collisionLayer;
        data.friction = comp.friction;
        data.restitution = comp.restitution;
        return data;
    }

    bool PhysicsComponentService::setColliderData(EntityHandle entity, const ColliderComponentData& colliderData)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ColliderComponent>())
        {
            sceneEntity.addComponent<components::ColliderComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::ColliderComponent>();
        comp.shape = colliderData.shape;
        comp.size = colliderData.size;
        comp.height = colliderData.height;
        comp.offset = colliderData.offset;
        comp.meshRef = colliderData.meshRef;
        comp.isTrigger = colliderData.isTrigger;
        comp.collisionLayer = colliderData.collisionLayer;
        comp.friction = colliderData.friction;
        comp.restitution = colliderData.restitution;
        return true;
    }

    // ========== RIGID BODY COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addRigidBodyComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::RigidBodyComponent>())
        {
            sceneEntity.addComponent<components::RigidBodyComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeRigidBodyComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::RigidBodyComponent>())
        {
            sceneEntity.removeComponent<components::RigidBodyComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasRigidBodyComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::RigidBodyComponent>();
    }

    std::optional<RigidBodyComponentData> PhysicsComponentService::getRigidBodyData(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::RigidBodyComponent>())
        {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::RigidBodyComponent>();
        RigidBodyComponentData data;
        data.type = comp.type;
        data.mass = comp.mass;
        data.linearDamping = comp.linearDamping;
        data.angularDamping = comp.angularDamping;
        data.freezePositionX = comp.freezePositionX;
        data.freezePositionY = comp.freezePositionY;
        data.freezePositionZ = comp.freezePositionZ;
        data.freezeRotationX = comp.freezeRotationX;
        data.freezeRotationY = comp.freezeRotationY;
        data.freezeRotationZ = comp.freezeRotationZ;
        return data;
    }

    bool PhysicsComponentService::setRigidBodyData(EntityHandle entity, const RigidBodyComponentData& rigidBodyData)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::RigidBodyComponent>())
        {
            sceneEntity.addComponent<components::RigidBodyComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::RigidBodyComponent>();
        comp.type = rigidBodyData.type;
        comp.mass = rigidBodyData.mass;
        comp.linearDamping = rigidBodyData.linearDamping;
        comp.angularDamping = rigidBodyData.angularDamping;
        comp.freezePositionX = rigidBodyData.freezePositionX;
        comp.freezePositionY = rigidBodyData.freezePositionY;
        comp.freezePositionZ = rigidBodyData.freezePositionZ;
        comp.freezeRotationX = rigidBodyData.freezeRotationX;
        comp.freezeRotationY = rigidBodyData.freezeRotationY;
        comp.freezeRotationZ = rigidBodyData.freezeRotationZ;
        return true;
    }

    // ========== PHYSICS ANIMATION COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addPhysicsAnimationComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::PhysicsAnimationComponent>())
        {
            sceneEntity.addComponent<components::PhysicsAnimationComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removePhysicsAnimationComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::PhysicsAnimationComponent>())
        {
            sceneEntity.removeComponent<components::PhysicsAnimationComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasPhysicsAnimationComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::PhysicsAnimationComponent>();
    }

    std::optional<PhysicsAnimationComponentData> PhysicsComponentService::getPhysicsAnimationData(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::PhysicsAnimationComponent>())
        {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::PhysicsAnimationComponent>();
        PhysicsAnimationComponentData data;
        data.physicsAnimationRef = comp.physicsAnimationRef;
        data.defaultMode = comp.config.defaultMode;
        data.collisionLayer = comp.config.collisionLayer;
        data.boneBodyMappings = comp.config.boneBodyMappings;
        data.jointLimits = comp.config.jointLimits;
        return data;
    }

    bool PhysicsComponentService::setPhysicsAnimationData(EntityHandle entity, const PhysicsAnimationComponentData& data)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::PhysicsAnimationComponent>())
        {
            sceneEntity.addComponent<components::PhysicsAnimationComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::PhysicsAnimationComponent>();
        comp.physicsAnimationRef = data.physicsAnimationRef;
        comp.config.defaultMode = data.defaultMode;
        comp.config.collisionLayer = data.collisionLayer;
        comp.config.boneBodyMappings = data.boneBodyMappings;
        comp.config.jointLimits = data.jointLimits;
        return true;
    }

    // ========== NAVMESH AGENT COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addNavmeshAgentComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::NavmeshAgentComponent>())
        {
            sceneEntity.addComponent<components::NavmeshAgentComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeNavmeshAgentComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::NavmeshAgentComponent>())
        {
            sceneEntity.removeComponent<components::NavmeshAgentComponent>();
            return true;
        }
        return false;
    }

    // ========== EVENT HANDLER REGISTRATION ==========

    void PhysicsComponentService::registerEventHandlers(events::EventDispatcher& dispatcher)
    {
        // Collider component handlers
        dispatcher.registerCommandHandler<events::scene::AddColliderComponentCommand>(
            [this](const events::scene::AddColliderComponentCommand& cmd)
            {
                return addColliderComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveColliderComponentCommand>(
            [this](const events::scene::RemoveColliderComponentCommand& cmd)
            {
                return removeColliderComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetColliderDataCommand>(
            [this](const events::scene::SetColliderDataCommand& cmd)
            {
                return setColliderData(cmd.entity, cmd.colliderData);
            });

        dispatcher.registerQueryHandler<events::scene::HasColliderComponentQuery>(
            [this](const events::scene::HasColliderComponentQuery& query)
            {
                return hasColliderComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetColliderDataQuery>(
            [this](const events::scene::GetColliderDataQuery& query)
            {
                return getColliderData(query.entity);
            });

        // RigidBody component handlers
        dispatcher.registerCommandHandler<events::scene::AddRigidBodyComponentCommand>(
            [this](const events::scene::AddRigidBodyComponentCommand& cmd)
            {
                return addRigidBodyComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveRigidBodyComponentCommand>(
            [this](const events::scene::RemoveRigidBodyComponentCommand& cmd)
            {
                return removeRigidBodyComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetRigidBodyDataCommand>(
            [this](const events::scene::SetRigidBodyDataCommand& cmd)
            {
                return setRigidBodyData(cmd.entity, cmd.rigidBodyData);
            });

        dispatcher.registerQueryHandler<events::scene::HasRigidBodyComponentQuery>(
            [this](const events::scene::HasRigidBodyComponentQuery& query)
            {
                return hasRigidBodyComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetRigidBodyDataQuery>(
            [this](const events::scene::GetRigidBodyDataQuery& query)
            {
                return getRigidBodyData(query.entity);
            });

        // PhysicsAnimation component handlers
        dispatcher.registerCommandHandler<events::scene::AddPhysicsAnimationComponentCommand>(
            [this](const events::scene::AddPhysicsAnimationComponentCommand& cmd)
            {
                return addPhysicsAnimationComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemovePhysicsAnimationComponentCommand>(
            [this](const events::scene::RemovePhysicsAnimationComponentCommand& cmd)
            {
                return removePhysicsAnimationComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetPhysicsAnimationDataCommand>(
            [this](const events::scene::SetPhysicsAnimationDataCommand& cmd)
            {
                return setPhysicsAnimationData(cmd.entity, cmd.data);
            });

        dispatcher.registerQueryHandler<events::scene::HasPhysicsAnimationComponentQuery>(
            [this](const events::scene::HasPhysicsAnimationComponentQuery& query)
            {
                return hasPhysicsAnimationComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetPhysicsAnimationDataQuery>(
            [this](const events::scene::GetPhysicsAnimationDataQuery& query)
            {
                return getPhysicsAnimationData(query.entity);
            });

        // NavmeshAgent component handlers
        dispatcher.registerCommandHandler<events::scene::AddNavmeshAgentComponentCommand>(
            [this](const events::scene::AddNavmeshAgentComponentCommand& cmd)
            {
                return addNavmeshAgentComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveNavmeshAgentComponentCommand>(
            [this](const events::scene::RemoveNavmeshAgentComponentCommand& cmd)
            {
                return removeNavmeshAgentComponent(cmd.entity);
            });

        // Off-Mesh Link component handlers
        dispatcher.registerCommandHandler<events::scene::AddOffMeshLinkComponentCommand>(
            [this](const events::scene::AddOffMeshLinkComponentCommand& cmd)
            {
                return addOffMeshLinkComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveOffMeshLinkComponentCommand>(
            [this](const events::scene::RemoveOffMeshLinkComponentCommand& cmd)
            {
                return removeOffMeshLinkComponent(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::scene::HasOffMeshLinkComponentQuery>(
            [this](const events::scene::HasOffMeshLinkComponentQuery& query)
            {
                return hasOffMeshLinkComponent(query.entity);
            });

        // Navmesh Obstacle component handlers
        dispatcher.registerCommandHandler<events::scene::AddNavmeshObstacleComponentCommand>(
            [this](const events::scene::AddNavmeshObstacleComponentCommand& cmd)
            {
                return addNavmeshObstacleComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveNavmeshObstacleComponentCommand>(
            [this](const events::scene::RemoveNavmeshObstacleComponentCommand& cmd)
            {
                return removeNavmeshObstacleComponent(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::scene::HasNavmeshObstacleComponentQuery>(
            [this](const events::scene::HasNavmeshObstacleComponentQuery& query)
            {
                return hasNavmeshObstacleComponent(query.entity);
            });

        // Navmesh Modifier Volume component handlers
        dispatcher.registerCommandHandler<events::scene::AddNavmeshModifierVolumeComponentCommand>(
            [this](const events::scene::AddNavmeshModifierVolumeComponentCommand& cmd)
            {
                return addNavmeshModifierVolumeComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveNavmeshModifierVolumeComponentCommand>(
            [this](const events::scene::RemoveNavmeshModifierVolumeComponentCommand& cmd)
            {
                return removeNavmeshModifierVolumeComponent(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::scene::HasNavmeshModifierVolumeComponentQuery>(
            [this](const events::scene::HasNavmeshModifierVolumeComponentQuery& query)
            {
                return hasNavmeshModifierVolumeComponent(query.entity);
            });

        // Controller component handlers
        dispatcher.registerCommandHandler<events::scene::AddControllerComponentCommand>(
            [this](const events::scene::AddControllerComponentCommand& cmd)
            {
                return addControllerComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveControllerComponentCommand>(
            [this](const events::scene::RemoveControllerComponentCommand& cmd)
            {
                return removeControllerComponent(cmd.entity);
            });
    }

    // ========== OFF-MESH LINK COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addOffMeshLinkComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::OffMeshLinkComponent>())
        {
            sceneEntity.addComponent<components::OffMeshLinkComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeOffMeshLinkComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::OffMeshLinkComponent>())
        {
            sceneEntity.removeComponent<components::OffMeshLinkComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasOffMeshLinkComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::OffMeshLinkComponent>();
    }

    // ========== NAVMESH OBSTACLE COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addNavmeshObstacleComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::NavmeshObstacleComponent>())
        {
            sceneEntity.addComponent<components::NavmeshObstacleComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeNavmeshObstacleComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::NavmeshObstacleComponent>())
        {
            sceneEntity.removeComponent<components::NavmeshObstacleComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasNavmeshObstacleComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::NavmeshObstacleComponent>();
    }

    // ========== NAVMESH MODIFIER VOLUME COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addNavmeshModifierVolumeComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::NavmeshModifierVolumeComponent>())
        {
            sceneEntity.addComponent<components::NavmeshModifierVolumeComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeNavmeshModifierVolumeComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::NavmeshModifierVolumeComponent>())
        {
            sceneEntity.removeComponent<components::NavmeshModifierVolumeComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasNavmeshModifierVolumeComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::NavmeshModifierVolumeComponent>();
    }

    // ========== CONTROLLER COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addControllerComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::ControllerComponent>())
        {
            sceneEntity.addComponent<components::ControllerComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeControllerComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::ControllerComponent>())
        {
            sceneEntity.removeComponent<components::ControllerComponent>();
            return true;
        }
        return false;
    }
}
