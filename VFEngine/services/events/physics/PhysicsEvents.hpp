#pragma once

#include "../EventTypes.hpp"
#include "../../interfaces/physics/IPhysicsService.hpp"
#include "../../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <optional>

namespace events::physics {

    struct SetGravityCommand : ::events::ICommand<void> {
        glm::vec3 gravity;
        std::string_view getName() const override { return "SetGravity"; }
    };

    struct AddRigidBodyCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        services::RigidBodyData rigidBody;
        services::ColliderData collider;
        std::string_view getName() const override { return "AddRigidBody"; }
    };

    struct RemoveRigidBodyCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveRigidBody"; }
    };

    struct AddColliderCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        services::ColliderData collider;
        std::string_view getName() const override { return "AddCollider"; }
    };

    struct RemoveColliderCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveCollider"; }
    };

    struct ApplyForceCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::vec3 force;
        std::string_view getName() const override { return "ApplyForce"; }
    };

    struct ApplyForceAtPositionCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::vec3 force;
        glm::vec3 position;
        std::string_view getName() const override { return "ApplyForceAtPosition"; }
    };

    struct ApplyImpulseCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::vec3 impulse;
        std::string_view getName() const override { return "ApplyImpulse"; }
    };

    struct ApplyTorqueCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::vec3 torque;
        std::string_view getName() const override { return "ApplyTorque"; }
    };

    struct SetLinearVelocityCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::vec3 velocity;
        std::string_view getName() const override { return "SetLinearVelocity"; }
    };

    struct SetAngularVelocityCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::vec3 velocity;
        std::string_view getName() const override { return "SetAngularVelocity"; }
    };

    struct SetPhysicsPositionCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::vec3 position;
        std::string_view getName() const override { return "SetPhysicsPosition"; }
    };

    struct SetPhysicsRotationCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::quat rotation;
        std::string_view getName() const override { return "SetPhysicsRotation"; }
    };

    struct GetGravityQuery : ::events::IQuery<glm::vec3> {
        std::string_view getName() const override { return "GetGravity"; }
    };

    struct HasRigidBodyQuery : ::events::IQuery<bool> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "HasRigidBody"; }
    };

    struct GetRigidBodyQuery : ::events::IQuery<std::optional<services::RigidBodyData>> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetRigidBody"; }
    };

    struct GetLinearVelocityQuery : ::events::IQuery<glm::vec3> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetLinearVelocity"; }
    };

    struct GetAngularVelocityQuery : ::events::IQuery<glm::vec3> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetAngularVelocity"; }
    };

    struct GetPhysicsPositionQuery : ::events::IQuery<glm::vec3> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetPhysicsPosition"; }
    };

    struct GetPhysicsRotationQuery : ::events::IQuery<glm::quat> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetPhysicsRotation"; }
    };

    struct RaycastQuery : ::events::IQuery<services::RaycastHit> {
        glm::vec3 origin;
        glm::vec3 direction;
        float maxDistance;
        uint16_t layerMask = 0xFFFF;
        std::string_view getName() const override { return "Raycast"; }
    };

    struct RaycastAllQuery : ::events::IQuery<std::vector<services::RaycastHit>> {
        glm::vec3 origin;
        glm::vec3 direction;
        float maxDistance;
        uint16_t layerMask = 0xFFFF;
        std::string_view getName() const override { return "RaycastAll"; }
    };

    struct IsOverlappingQuery : ::events::IQuery<bool> {
        services::EntityHandle entityA;
        services::EntityHandle entityB;
        std::string_view getName() const override { return "IsOverlapping"; }
    };

    struct GetMassQuery : ::events::IQuery<float> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetMass"; }
    };

    struct GetLinearDampingQuery : ::events::IQuery<float> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetLinearDamping"; }
    };

    struct GetAngularDampingQuery : ::events::IQuery<float> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetAngularDamping"; }
    };

    struct GetBodyTypeQuery : ::events::IQuery<services::RigidBodyData::Type> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetBodyType"; }
    };

    struct IsBodySleepingQuery : ::events::IQuery<bool> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "IsBodySleeping"; }
    };

    struct CollisionStartNotification : ::events::INotification {
        services::EntityHandle entityA;
        services::EntityHandle entityB;
        glm::vec3 contactPoint;
        glm::vec3 normal;
        float penetrationDepth;
        std::string_view getName() const override { return "CollisionStart"; }
    };

    struct CollisionEndNotification : ::events::INotification {
        services::EntityHandle entityA;
        services::EntityHandle entityB;
        std::string_view getName() const override { return "CollisionEnd"; }
    };

    struct TriggerEnterNotification : ::events::INotification {
        services::EntityHandle triggerEntity;
        services::EntityHandle otherEntity;
        std::string_view getName() const override { return "TriggerEnter"; }
    };

    struct TriggerExitNotification : ::events::INotification {
        services::EntityHandle triggerEntity;
        services::EntityHandle otherEntity;
        std::string_view getName() const override { return "TriggerExit"; }
    };

    struct AddTerrainColliderCommand : ::events::ICommand<bool> {
        services::EntityHandle terrainEntity;
        std::string_view getName() const override { return "AddTerrainCollider"; }
    };

    struct RemoveTerrainColliderCommand : ::events::ICommand<void> {
        services::EntityHandle terrainEntity;
        std::string_view getName() const override { return "RemoveTerrainCollider"; }
    };

    struct HasTerrainColliderQuery : ::events::IQuery<bool> {
        services::EntityHandle terrainEntity;
        std::string_view getName() const override { return "HasTerrainCollider"; }
    };

    struct PhysicsColliderStreamConfigData
    {
        float memoryBudgetMB = 64.0f;
        int maxCreationsPerFrame = 4;
        float lodDistance0 = 100.0f; // full res
        float lodDistance1 = 300.0f; // half res
        float lodDistance2 = 600.0f; // quarter res
    };

    struct SetPhysicsColliderStreamConfigCommand : ::events::ICommand<> {
        services::EntityHandle terrainEntity;
        float memoryBudgetMB = 64.0f;
        int maxCreationsPerFrame = 4;
        float lodDistance0 = 100.0f;
        float lodDistance1 = 300.0f;
        float lodDistance2 = 600.0f;
        std::string_view getName() const override { return "SetPhysicsColliderStreamConfig"; }
    };

    struct GetPhysicsColliderStreamConfigQuery : ::events::IQuery<PhysicsColliderStreamConfigData> {
        services::EntityHandle terrainEntity;
        std::string_view getName() const override { return "GetPhysicsColliderStreamConfig"; }
    };

}
