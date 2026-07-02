#include "PhysicsComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../events/physics/PhysicsAnimationEvents.hpp"

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
            // VK-1351: tear down the Jolt body so it doesn't leak when the collider is removed
            // at runtime. The handler no-ops if no body exists, so this is safe.
            events::physics::RemoveRigidBodyCommand removeBody;
            removeBody.entity = entity;
            ::events::EventDispatcher::instance().execute(removeBody);
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
        data.submeshIndex = comp.submeshIndex;
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
        comp.submeshIndex = colliderData.submeshIndex;
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
            // VK-1351: tear down the Jolt body so it doesn't leak when the rigid body is
            // removed at runtime. The handler no-ops if no body exists, so this is safe.
            events::physics::RemoveRigidBodyCommand removeBody;
            removeBody.entity = entity;
            ::events::EventDispatcher::instance().execute(removeBody);
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

    // ========== VEHICLE COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addVehicleComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VehicleComponent>())
        {
            sceneEntity.addComponent<components::VehicleComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeVehicleComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::VehicleComponent>())
        {
            sceneEntity.removeComponent<components::VehicleComponent>();
            events::physics::DestroyVehicleCommand destroyCmd;
            destroyCmd.entity = entity;
            ::events::EventDispatcher::instance().execute(destroyCmd);
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasVehicleComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::VehicleComponent>();
    }

    std::optional<VehicleComponentData> PhysicsComponentService::getVehicleData(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VehicleComponent>())
        {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::VehicleComponent>();
        VehicleComponentData data;
        data.config = comp.config;
        return data;
    }

    bool PhysicsComponentService::setVehicleData(EntityHandle entity, const VehicleComponentData& vehicleData)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VehicleComponent>())
        {
            sceneEntity.addComponent<components::VehicleComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::VehicleComponent>();
        comp.config = vehicleData.config;
        return true;
    }

    // ========== BUOYANCY COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addBuoyancyComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BuoyancyComponent>())
        {
            sceneEntity.addComponent<components::BuoyancyComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeBuoyancyComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::BuoyancyComponent>())
        {
            sceneEntity.removeComponent<components::BuoyancyComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasBuoyancyComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::BuoyancyComponent>();
    }

    std::optional<BuoyancyComponentData> PhysicsComponentService::getBuoyancyData(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BuoyancyComponent>())
        {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::BuoyancyComponent>();
        BuoyancyComponentData data;
        data.customSampleMode = comp.sampleMode == components::BuoyancyComponent::SampleMode::Custom;
        data.customPointCount = comp.customPointCount;
        for (uint32_t i = 0; i < 8; ++i)
        {
            data.customPoints[i] = comp.customPoints[i];
        }
        data.buoyancyScale = comp.buoyancyScale;
        data.angularDrag = comp.angularDrag;
        return data;
    }

    bool PhysicsComponentService::setBuoyancyData(EntityHandle entity, const BuoyancyComponentData& buoyancyData)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BuoyancyComponent>())
        {
            sceneEntity.addComponent<components::BuoyancyComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::BuoyancyComponent>();
        comp.sampleMode = buoyancyData.customSampleMode
                              ? components::BuoyancyComponent::SampleMode::Custom
                              : components::BuoyancyComponent::SampleMode::Auto;
        comp.customPointCount = buoyancyData.customPointCount > 8 ? 8 : buoyancyData.customPointCount;
        for (uint32_t i = 0; i < 8; ++i)
        {
            comp.customPoints[i] = buoyancyData.customPoints[i];
        }
        comp.buoyancyScale = buoyancyData.buoyancyScale;
        comp.angularDrag = buoyancyData.angularDrag;
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
            // VK-1437: tear down any provider-side ragdoll/kinematic state so it doesn't leak when the
            // component is removed at runtime. The handler no-ops if no state exists, so this is safe.
            events::physicsAnimation::DestroyPhysicsAnimationCommand destroyCmd;
            destroyCmd.entity = entity;
            ::events::EventDispatcher::instance().execute(destroyCmd);
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
        data.config = comp.config;
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
        comp.config = data.config;
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

        // Vehicle component handlers
        dispatcher.registerCommandHandler<events::scene::AddVehicleComponentCommand>(
            [this](const events::scene::AddVehicleComponentCommand& cmd)
            {
                return addVehicleComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveVehicleComponentCommand>(
            [this](const events::scene::RemoveVehicleComponentCommand& cmd)
            {
                return removeVehicleComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetVehicleDataCommand>(
            [this](const events::scene::SetVehicleDataCommand& cmd)
            {
                return setVehicleData(cmd.entity, cmd.vehicleData);
            });

        dispatcher.registerQueryHandler<events::scene::HasVehicleComponentQuery>(
            [this](const events::scene::HasVehicleComponentQuery& query)
            {
                return hasVehicleComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetVehicleDataQuery>(
            [this](const events::scene::GetVehicleDataQuery& query)
            {
                return getVehicleData(query.entity);
            });

        // Buoyancy component handlers
        dispatcher.registerCommandHandler<events::scene::AddBuoyancyComponentCommand>(
            [this](const events::scene::AddBuoyancyComponentCommand& cmd)
            {
                return addBuoyancyComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveBuoyancyComponentCommand>(
            [this](const events::scene::RemoveBuoyancyComponentCommand& cmd)
            {
                return removeBuoyancyComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetBuoyancyDataCommand>(
            [this](const events::scene::SetBuoyancyDataCommand& cmd)
            {
                return setBuoyancyData(cmd.entity, cmd.buoyancyData);
            });

        dispatcher.registerQueryHandler<events::scene::HasBuoyancyComponentQuery>(
            [this](const events::scene::HasBuoyancyComponentQuery& query)
            {
                return hasBuoyancyComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetBuoyancyDataQuery>(
            [this](const events::scene::GetBuoyancyDataQuery& query)
            {
                return getBuoyancyData(query.entity);
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

        // Nav Invoker component handlers
        dispatcher.registerCommandHandler<events::scene::AddNavInvokerComponentCommand>(
            [this](const events::scene::AddNavInvokerComponentCommand& cmd)
            {
                return addNavInvokerComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveNavInvokerComponentCommand>(
            [this](const events::scene::RemoveNavInvokerComponentCommand& cmd)
            {
                return removeNavInvokerComponent(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::scene::HasNavInvokerComponentQuery>(
            [this](const events::scene::HasNavInvokerComponentQuery& query)
            {
                return hasNavInvokerComponent(query.entity);
            });

        // Volumetric Nav Volume component handlers
        dispatcher.registerCommandHandler<events::scene::AddVolumetricNavVolumeComponentCommand>(
            [this](const events::scene::AddVolumetricNavVolumeComponentCommand& cmd)
            {
                return addVolumetricNavVolumeComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveVolumetricNavVolumeComponentCommand>(
            [this](const events::scene::RemoveVolumetricNavVolumeComponentCommand& cmd)
            {
                return removeVolumetricNavVolumeComponent(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::scene::HasVolumetricNavVolumeComponentQuery>(
            [this](const events::scene::HasVolumetricNavVolumeComponentQuery& query)
            {
                return hasVolumetricNavVolumeComponent(query.entity);
            });

        // Volumetric Agent component handlers
        dispatcher.registerCommandHandler<events::scene::AddVolumetricAgentComponentCommand>(
            [this](const events::scene::AddVolumetricAgentComponentCommand& cmd)
            {
                return addVolumetricAgentComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveVolumetricAgentComponentCommand>(
            [this](const events::scene::RemoveVolumetricAgentComponentCommand& cmd)
            {
                return removeVolumetricAgentComponent(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::scene::HasVolumetricAgentComponentQuery>(
            [this](const events::scene::HasVolumetricAgentComponentQuery& query)
            {
                return hasVolumetricAgentComponent(query.entity);
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

        // Destructible component handlers
        dispatcher.registerCommandHandler<events::scene::AddDestructibleComponentCommand>(
            [this](const events::scene::AddDestructibleComponentCommand& cmd)
            {
                return addDestructibleComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveDestructibleComponentCommand>(
            [this](const events::scene::RemoveDestructibleComponentCommand& cmd)
            {
                return removeDestructibleComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scene::SetDestructibleDataCommand>(
            [this](const events::scene::SetDestructibleDataCommand& cmd)
            {
                return setDestructibleData(cmd.entity, cmd.data);
            });

        dispatcher.registerQueryHandler<events::scene::HasDestructibleComponentQuery>(
            [this](const events::scene::HasDestructibleComponentQuery& query)
            {
                return hasDestructibleComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::GetDestructibleDataQuery>(
            [this](const events::scene::GetDestructibleDataQuery& query)
            {
                return getDestructibleData(query.entity);
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

    // ========== NAV INVOKER COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addNavInvokerComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::NavInvokerComponent>())
        {
            sceneEntity.addComponent<components::NavInvokerComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeNavInvokerComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::NavInvokerComponent>())
        {
            sceneEntity.removeComponent<components::NavInvokerComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasNavInvokerComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::NavInvokerComponent>();
    }

    // ========== VOLUMETRIC NAV VOLUME COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addVolumetricNavVolumeComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VolumetricNavVolumeComponent>())
        {
            sceneEntity.addComponent<components::VolumetricNavVolumeComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeVolumetricNavVolumeComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::VolumetricNavVolumeComponent>())
        {
            sceneEntity.removeComponent<components::VolumetricNavVolumeComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasVolumetricNavVolumeComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::VolumetricNavVolumeComponent>();
    }

    // ========== VOLUMETRIC AGENT COMPONENT OPERATIONS ==========

    bool PhysicsComponentService::addVolumetricAgentComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::VolumetricAgentComponent>())
        {
            sceneEntity.addComponent<components::VolumetricAgentComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeVolumetricAgentComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::VolumetricAgentComponent>())
        {
            sceneEntity.removeComponent<components::VolumetricAgentComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasVolumetricAgentComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::VolumetricAgentComponent>();
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

    // ========== DESTRUCTIBLE COMPONENT ==========

    bool PhysicsComponentService::addDestructibleComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DestructibleComponent>())
        {
            sceneEntity.addComponent<components::DestructibleComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::removeDestructibleComponent(EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::DestructibleComponent>())
        {
            sceneEntity.removeComponent<components::DestructibleComponent>();
            return true;
        }
        return false;
    }

    bool PhysicsComponentService::hasDestructibleComponent(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::DestructibleComponent>();
    }

    std::optional<DestructibleComponentData> PhysicsComponentService::getDestructibleData(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DestructibleComponent>())
        {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::DestructibleComponent>();
        DestructibleComponentData data;
        data.maxHealth = comp.maxHealth;
        data.destructionThreshold = comp.destructionThreshold;
        data.fractureAssetRef = comp.fractureAssetRef;
        data.fragmentCount = comp.fragmentCount;
        data.mode = comp.mode;
        data.damageFilter = comp.damageFilter;
        data.fragmentMassTotal = comp.fragmentMassTotal;
        data.fragmentLifetime = comp.fragmentLifetime;
        data.propagationRadius = comp.propagationRadius;
        data.propagationDamage = comp.propagationDamage;
        data.onDamageVFX = comp.onDamageVFX;
        data.onDestroyVFX = comp.onDestroyVFX;
        data.onDamageAudio = comp.onDamageAudio;
        data.onDestroyAudio = comp.onDestroyAudio;
        data.fragmentCollisionAudio = comp.fragmentCollisionAudio;
        data.damageDecalAlbedo = comp.damageDecalAlbedo;
        data.damageDecalNormal = comp.damageDecalNormal;
        data.decalHalfExtents = comp.decalHalfExtents;
        return data;
    }

    bool PhysicsComponentService::setDestructibleData(EntityHandle entity, const DestructibleComponentData& data)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::DestructibleComponent>())
        {
            sceneEntity.addComponent<components::DestructibleComponent>();
        }

        auto& comp = sceneEntity.getComponent<components::DestructibleComponent>();
        comp.maxHealth = data.maxHealth;
        comp.currentHealth = data.maxHealth;
        comp.destructionThreshold = data.destructionThreshold;
        comp.fractureAssetRef = data.fractureAssetRef;
        comp.fragmentCount = data.fragmentCount;
        comp.mode = data.mode;
        comp.damageFilter = data.damageFilter;
        comp.fragmentMassTotal = data.fragmentMassTotal;
        comp.fragmentLifetime = data.fragmentLifetime;
        comp.propagationRadius = data.propagationRadius;
        comp.propagationDamage = data.propagationDamage;
        comp.onDamageVFX = data.onDamageVFX;
        comp.onDestroyVFX = data.onDestroyVFX;
        comp.onDamageAudio = data.onDamageAudio;
        comp.onDestroyAudio = data.onDestroyAudio;
        comp.fragmentCollisionAudio = data.fragmentCollisionAudio;
        comp.damageDecalAlbedo = data.damageDecalAlbedo;
        comp.damageDecalNormal = data.damageDecalNormal;
        comp.decalHalfExtents = data.decalHalfExtents;
        return true;
    }
}
