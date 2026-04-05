#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include "types/PhysicsTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <cstdint>
#include <string>

namespace core::physics
{
    struct PhysicsContext;
    class PhysicsBodyRegistry;

    using BodyType = types::RigidBodyType;
    using ColliderShape = types::ColliderShape;

    struct RigidBodyCreateInfo
    {
        BodyType type = BodyType::Dynamic;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        float mass = 1.0f;
        float friction = 0.2f;
        float restitution = 0.0f;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        glm::vec3 linearVelocity{0.0f};
        glm::vec3 angularVelocity{0.0f};
        bool activate = true;
    };

    struct ColliderCreateInfo
    {
        ColliderShape shape = ColliderShape::Box;
        glm::vec3 halfExtents{0.5f};
        float radius = 0.5f;
        float height = 1.0f;
        glm::vec3 offset{0.0f};
        bool isTrigger = false;
        uint8_t collisionLayer = 1;
        std::string meshPath;
        int32_t submeshIndex = -1;
    };

    struct RaycastResult
    {
        bool hit = false;
        uint64_t entityId = 0;
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f};
        float distance = 0.0f;
    };

    class PhysicsRigidBodyManager
    {
    public:
        void init(PhysicsContext* context, PhysicsBodyRegistry* registry);

        JPH::BodyID addRigidBody(uint64_t entityId, const RigidBodyCreateInfo& bodyInfo,
                                 const ColliderCreateInfo& colliderInfo);
        void removeRigidBody(JPH::BodyID bodyId);

        glm::vec3 getPosition(JPH::BodyID bodyId) const;
        glm::quat getRotation(JPH::BodyID bodyId) const;
        void setPosition(JPH::BodyID bodyId, const glm::vec3& position);
        void setRotation(JPH::BodyID bodyId, const glm::quat& rotation);

        void setLinearVelocity(JPH::BodyID bodyId, const glm::vec3& velocity);
        glm::vec3 getLinearVelocity(JPH::BodyID bodyId) const;
        void setAngularVelocity(JPH::BodyID bodyId, const glm::vec3& velocity);
        glm::vec3 getAngularVelocity(JPH::BodyID bodyId) const;

        BodyType getBodyType(JPH::BodyID bodyId) const;
        float getMass(JPH::BodyID bodyId) const;
        float getLinearDamping(JPH::BodyID bodyId) const;
        float getAngularDamping(JPH::BodyID bodyId) const;

        void applyForce(JPH::BodyID bodyId, const glm::vec3& force);
        void applyForceAtPosition(JPH::BodyID bodyId, const glm::vec3& force,
                                  const glm::vec3& position);
        void applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse);
        void applyTorque(JPH::BodyID bodyId, const glm::vec3& torque);

        RaycastResult raycast(const glm::vec3& origin, const glm::vec3& direction,
                              float maxDistance, uint16_t layerMask = 0xFFFF) const;
        std::vector<RaycastResult> raycastAll(const glm::vec3& origin, const glm::vec3& direction,
                                              float maxDistance, uint16_t layerMask = 0xFFFF) const;
        bool areBodiesInContact(JPH::BodyID bodyA, JPH::BodyID bodyB) const;
        bool isBodyActive(JPH::BodyID bodyId) const;

    private:
        PhysicsContext* ctx = nullptr;
        PhysicsBodyRegistry* bodyRegistry = nullptr;
    };
}
