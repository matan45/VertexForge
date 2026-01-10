#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include "PhysicsLayers.hpp"
#include "PhysicsContactListener.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <string>

namespace core::physics {

    // Body type enum matching services layer
    enum class BodyType {
        Static,
        Dynamic,
        Kinematic
    };

    // Collider shape enum (must match types::ColliderShape values)
    enum class ColliderShape {
        Box = 0,
        Sphere = 1,
        Capsule = 2,
        ConvexMesh = 3,
        TriangleMesh = 4
    };

    struct RigidBodyCreateInfo {
        BodyType type = BodyType::Dynamic;
        glm::vec3 position{ 0.0f };
        glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        float mass = 1.0f;
        float friction = 0.2f;
        float restitution = 0.0f;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        glm::vec3 linearVelocity{ 0.0f };
        glm::vec3 angularVelocity{ 0.0f };
    };

    struct ColliderCreateInfo {
        ColliderShape shape = ColliderShape::Box;
        glm::vec3 halfExtents{ 0.5f };  // Box half-extents
        float radius = 0.5f;            // Sphere/Capsule radius
        float height = 1.0f;            // Capsule total height
        glm::vec3 offset{ 0.0f };       // Local offset
        bool isTrigger = false;
        uint8_t collisionLayer = 1;     // Collision layer (0-15, default 1 = Dynamic)
        std::string meshPath;           // Path to mesh for ConvexMesh/TriangleMesh shapes
    };

    struct RaycastResult {
        bool hit = false;
        uint64_t entityId = 0;
        glm::vec3 point{ 0.0f };
        glm::vec3 normal{ 0.0f };
        float distance = 0.0f;
    };

    class PhysicsWorld {
    public:
        PhysicsWorld();
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        // Lifecycle
        bool init();
        void cleanUp();
        bool isInitialized() const { return initialized; }

        // Simulation
        void step(float deltaTime, int collisionSteps = 1);

        // Process contact events (call after step, from main thread)
        void processContactEvents();

        // Gravity
        void setGravity(const glm::vec3& gravity);
        glm::vec3 getGravity() const;

        // Body Management
        JPH::BodyID addRigidBody(uint64_t entityId, const RigidBodyCreateInfo& bodyInfo,
            const ColliderCreateInfo& colliderInfo);
        void removeRigidBody(JPH::BodyID bodyId);
        void removeRigidBodyByEntity(uint64_t entityId);
        bool hasBody(JPH::BodyID bodyId) const;
        bool hasEntityBody(uint64_t entityId) const;

        // Body State - Position/Rotation
        glm::vec3 getPosition(JPH::BodyID bodyId) const;
        glm::quat getRotation(JPH::BodyID bodyId) const;
        void setPosition(JPH::BodyID bodyId, const glm::vec3& position);
        void setRotation(JPH::BodyID bodyId, const glm::quat& rotation);

        // Velocity
        void setLinearVelocity(JPH::BodyID bodyId, const glm::vec3& velocity);
        glm::vec3 getLinearVelocity(JPH::BodyID bodyId) const;
        void setAngularVelocity(JPH::BodyID bodyId, const glm::vec3& velocity);
        glm::vec3 getAngularVelocity(JPH::BodyID bodyId) const;

        // Body Properties (for getRigidBody queries)
        BodyType getBodyType(JPH::BodyID bodyId) const;
        float getMass(JPH::BodyID bodyId) const;
        float getFriction(JPH::BodyID bodyId) const;
        float getRestitution(JPH::BodyID bodyId) const;
        float getLinearDamping(JPH::BodyID bodyId) const;
        float getAngularDamping(JPH::BodyID bodyId) const;

        // Collision matrix configuration
        void setCollisionMatrix(const std::array<std::bitset<MAX_COLLISION_LAYERS>, MAX_COLLISION_LAYERS>& matrix);
        DynamicObjectLayerPairFilter* getObjectLayerPairFilter() { return objectLayerPairFilter.get(); }

        // Forces
        void applyForce(JPH::BodyID bodyId, const glm::vec3& force);
        void applyForceAtPosition(JPH::BodyID bodyId, const glm::vec3& force,
            const glm::vec3& position);
        void applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse);
        void applyTorque(JPH::BodyID bodyId, const glm::vec3& torque);

        // Queries
        RaycastResult raycast(const glm::vec3& origin, const glm::vec3& direction,
            float maxDistance) const;
        std::vector<RaycastResult> raycastAll(const glm::vec3& origin,
            const glm::vec3& direction,
            float maxDistance) const;
        bool areBodiesInContact(JPH::BodyID bodyA, JPH::BodyID bodyB) const;

        // Entity-Body mapping
        JPH::BodyID getBodyForEntity(uint64_t entityId) const;
        uint64_t getEntityForBody(JPH::BodyID bodyId) const;

        // Contact callbacks
        void setContactAddedCallback(ContactCallback callback);
        void setContactRemovedCallback(ContactCallback callback);

        // Direct access (for advanced use)
        JPH::PhysicsSystem* getPhysicsSystem() { return physicsSystem.get(); }

    private:
        // Jolt systems
        std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JPH::PhysicsSystem> physicsSystem;

        // Layer interfaces (must outlive PhysicsSystem)
        std::unique_ptr<BroadPhaseLayerInterfaceImpl> broadPhaseLayerInterface;
        std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> objectVsBroadPhaseFilter;
        std::unique_ptr<ObjectLayerPairFilterImpl> objectLayerPairFilter;

        // Contact listener
        std::unique_ptr<PhysicsContactListener> contactListener;

        // Entity <-> Body mappings
        std::unordered_map<uint64_t, JPH::BodyID> entityToBody;
        std::unordered_map<uint32_t, uint64_t> bodyToEntity;  // BodyID index to entity

        bool initialized = false;

        // Helper methods
        JPH::Ref<JPH::Shape> createShape(const ColliderCreateInfo& info);
        JPH::ObjectLayer getObjectLayer(BodyType type, bool isTrigger);
        JPH::EMotionType getMotionType(BodyType type);

        // Type conversions
        static JPH::Vec3 toJolt(const glm::vec3& v);
        static JPH::Quat toJolt(const glm::quat& q);
        static JPH::RVec3 toJoltR(const glm::vec3& v);
        static glm::vec3 toGlm(const JPH::Vec3& v);
        static glm::vec3 toGlmR(const JPH::RVec3& v);
        static glm::quat toGlm(const JPH::Quat& q);
    };

}
