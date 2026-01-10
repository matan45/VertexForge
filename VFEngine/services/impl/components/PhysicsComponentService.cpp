#include "PhysicsComponentService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/SceneEvents.hpp"

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
        data.meshPath = comp.meshPath;
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
        comp.meshPath = colliderData.meshPath;
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
    }
}
