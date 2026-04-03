#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <memory>
#include <optional>

namespace scene {
    class SceneGraphSystem;
}

namespace events {
    class EventDispatcher;
}

namespace services {

    class PhysicsComponentService {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

    public:
        explicit PhysicsComponentService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // Collider Component Operations
        bool addColliderComponent(EntityHandle entity);
        bool removeColliderComponent(EntityHandle entity);
        bool hasColliderComponent(EntityHandle entity) const;
        std::optional<ColliderComponentData> getColliderData(EntityHandle entity) const;
        bool setColliderData(EntityHandle entity, const ColliderComponentData& colliderData);

        // RigidBody Component Operations
        bool addRigidBodyComponent(EntityHandle entity);
        bool removeRigidBodyComponent(EntityHandle entity);
        bool hasRigidBodyComponent(EntityHandle entity) const;
        std::optional<RigidBodyComponentData> getRigidBodyData(EntityHandle entity) const;
        bool setRigidBodyData(EntityHandle entity, const RigidBodyComponentData& rigidBodyData);

        // PhysicsAnimation Component Operations
        bool addPhysicsAnimationComponent(EntityHandle entity);
        bool removePhysicsAnimationComponent(EntityHandle entity);
        bool hasPhysicsAnimationComponent(EntityHandle entity) const;
        std::optional<PhysicsAnimationComponentData> getPhysicsAnimationData(EntityHandle entity) const;
        bool setPhysicsAnimationData(EntityHandle entity, const PhysicsAnimationComponentData& data);

        // NavmeshAgent Component Operations
        bool addNavmeshAgentComponent(EntityHandle entity);
        bool removeNavmeshAgentComponent(EntityHandle entity);

        // Off-Mesh Link Component Operations
        bool addOffMeshLinkComponent(EntityHandle entity);
        bool removeOffMeshLinkComponent(EntityHandle entity);
        bool hasOffMeshLinkComponent(EntityHandle entity) const;

        // Navmesh Obstacle Component Operations
        bool addNavmeshObstacleComponent(EntityHandle entity);
        bool removeNavmeshObstacleComponent(EntityHandle entity);
        bool hasNavmeshObstacleComponent(EntityHandle entity) const;

        // Navmesh Modifier Volume Component Operations
        bool addNavmeshModifierVolumeComponent(EntityHandle entity);
        bool removeNavmeshModifierVolumeComponent(EntityHandle entity);
        bool hasNavmeshModifierVolumeComponent(EntityHandle entity) const;

        // Nav Invoker Component Operations
        bool addNavInvokerComponent(EntityHandle entity);
        bool removeNavInvokerComponent(EntityHandle entity);
        bool hasNavInvokerComponent(EntityHandle entity) const;

        // Volumetric Nav Volume Component Operations
        bool addVolumetricNavVolumeComponent(EntityHandle entity);
        bool removeVolumetricNavVolumeComponent(EntityHandle entity);
        bool hasVolumetricNavVolumeComponent(EntityHandle entity) const;

        // Volumetric Agent Component Operations
        bool addVolumetricAgentComponent(EntityHandle entity);
        bool removeVolumetricAgentComponent(EntityHandle entity);
        bool hasVolumetricAgentComponent(EntityHandle entity) const;

        // Controller Component Operations
        bool addControllerComponent(EntityHandle entity);
        bool removeControllerComponent(EntityHandle entity);

        // Destructible Component Operations
        bool addDestructibleComponent(EntityHandle entity);
        bool removeDestructibleComponent(EntityHandle entity);
        bool hasDestructibleComponent(EntityHandle entity) const;
        std::optional<DestructibleComponentData> getDestructibleData(EntityHandle entity) const;
        bool setDestructibleData(EntityHandle entity, const DestructibleComponentData& data);
    };

}
