#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include "PhysicsLayers.hpp"
#include "PhysicsContactListener.hpp"
#include "types/PhysicsTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <string>

namespace core::physics
{
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
    };

    struct RaycastResult
    {
        bool hit = false;
        uint64_t entityId = 0;
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f};
        float distance = 0.0f;
    };

    struct TerrainHeightFieldCreateInfo
    {
        const float* heightSamples = nullptr;
        uint32_t sampleCount = 0;
        glm::vec3 offset{0.0f};
        glm::vec3 scale{1.0f};
        float friction = 0.5f;
        float restitution = 0.0f;
        uint8_t collisionLayer = 0;
    };

    class PhysicsWorld
    {
    private:
        std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JPH::PhysicsSystem> physicsSystem;
        
        std::unique_ptr<BroadPhaseLayerInterfaceImpl> broadPhaseLayerInterface;
        std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> objectVsBroadPhaseFilter;
        std::unique_ptr<ObjectLayerPairFilterImpl> objectLayerPairFilter;
        
        std::unique_ptr<PhysicsContactListener> contactListener;
        
        std::unordered_map<uint64_t, JPH::BodyID> entityToBody;
        std::unordered_map<uint32_t, uint64_t> bodyToEntity; // BodyID index to entity

        using TileCoordKey = uint64_t;
        std::unordered_map<uint64_t, std::unordered_map<TileCoordKey, JPH::BodyID>> terrainBodies;
        static TileCoordKey makeTileKey(int32_t x, int32_t z);

        bool initialized = false;

    public:
        explicit PhysicsWorld();
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        bool init();
        void cleanUp();
        bool isInitialized() const { return initialized; }

        void step(float deltaTime, int collisionSteps = 1);

        void processContactEvents();

        void setGravity(const glm::vec3& gravity);
        glm::vec3 getGravity() const;

        JPH::BodyID addRigidBody(uint64_t entityId, const RigidBodyCreateInfo& bodyInfo,
                                 const ColliderCreateInfo& colliderInfo);
        void removeRigidBody(JPH::BodyID bodyId);
        void removeRigidBodyByEntity(uint64_t entityId);
        bool hasEntityBody(uint64_t entityId) const;

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

        void setCollisionMatrix(const std::array<std::bitset<MAX_COLLISION_LAYERS>, MAX_COLLISION_LAYERS>& matrix);

        void applyForce(JPH::BodyID bodyId, const glm::vec3& force);
        void applyForceAtPosition(JPH::BodyID bodyId, const glm::vec3& force,
                                  const glm::vec3& position);
        void applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse);
        void applyTorque(JPH::BodyID bodyId, const glm::vec3& torque);

        RaycastResult raycast(const glm::vec3& origin, const glm::vec3& direction,
                              float maxDistance) const;
        bool areBodiesInContact(JPH::BodyID bodyA, JPH::BodyID bodyB) const;

        JPH::BodyID getBodyForEntity(uint64_t entityId) const;
        uint64_t getEntityForBody(JPH::BodyID bodyId) const;

        JPH::BodyID addTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                        const TerrainHeightFieldCreateInfo& info);
        void removeTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ);
        void removeAllTerrainBodies(uint64_t entityId);
        bool hasTerrainBodies(uint64_t entityId) const;

        void setContactAddedCallback(ContactCallback callback);
        void setContactRemovedCallback(ContactCallback callback);

    private:
        JPH::Ref<JPH::Shape> createShape(const ColliderCreateInfo& info);
        JPH::EMotionType getMotionType(BodyType type);

        static JPH::Vec3 toJolt(const glm::vec3& v);
        static JPH::Quat toJolt(const glm::quat& q);
        static JPH::RVec3 toJoltR(const glm::vec3& v);
        static glm::vec3 toGlm(const JPH::Vec3& v);
        static glm::vec3 toGlmR(const JPH::RVec3& v);
        static glm::quat toGlm(const JPH::Quat& q);
    };
}
