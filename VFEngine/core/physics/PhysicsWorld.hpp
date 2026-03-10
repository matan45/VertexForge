#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/SkeletonPose.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include "PhysicsLayers.hpp"
#include "PhysicsContactListener.hpp"
#include "PhysicsSkeletonConverter.hpp"
#include "types/PhysicsTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <string>
#include "JoltConversions.hpp"

namespace core::physics
{
    struct RagdollBuildResult;

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

    struct RagdollInstanceData
    {
        JPH::Ref<JPH::Ragdoll> ragdoll;
        SkeletonConversionResult skeletonConversion;
        uint32_t collisionGroupId = 0;
    };

    struct CharacterCreateInfo
    {
        types::ColliderShape shape = types::ColliderShape::Capsule;
        glm::vec3 size{1.0f};        // Box half-extents / Sphere: x=radius / Capsule: x=radius, y=height
        float maxSlopeAngle = 45.0f;
        float stepHeight = 0.35f;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        uint8_t collisionLayer = 1;
    };

    struct CharacterUpdateResult
    {
        glm::vec3 position{0.0f};
        glm::vec3 linearVelocity{0.0f};
        bool isGrounded = false;
        glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
        glm::vec3 groundVelocity{0.0f};
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
        std::unordered_map<uint32_t, uint64_t> bodyToEntity;

        using TileCoordKey = uint64_t;
        std::unordered_map<uint64_t, std::unordered_map<TileCoordKey, JPH::BodyID>> terrainBodies;
        static TileCoordKey makeTileKey(int32_t x, int32_t z);

        // Vegetation collider bodies per tile
        std::unordered_map<TileCoordKey, std::vector<JPH::BodyID>> vegetationBodies;

        std::unordered_map<uint64_t, RagdollInstanceData> entityRagdolls;
        std::unordered_map<uint64_t, std::vector<JPH::BodyID>> entityBoneBodies;
        std::unordered_map<uint32_t, int> bodyToBoneIndex;
        uint32_t nextCollisionGroupId = 1;

        std::unordered_map<uint64_t, JPH::Ref<JPH::CharacterVirtual>> entityCharacters;
        std::unordered_map<uint64_t, uint8_t> entityCharacterLayers;

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
                              float maxDistance, uint16_t layerMask = 0xFFFF) const;
        std::vector<RaycastResult> raycastAll(const glm::vec3& origin, const glm::vec3& direction,
                                              float maxDistance, uint16_t layerMask = 0xFFFF) const;
        bool areBodiesInContact(JPH::BodyID bodyA, JPH::BodyID bodyB) const;

        JPH::BodyID getBodyForEntity(uint64_t entityId) const;
        uint64_t getEntityForBody(JPH::BodyID bodyId) const;

        JPH::BodyID addTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                        const TerrainHeightFieldCreateInfo& info);
        void removeTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ);
        void removeAllTerrainBodies(uint64_t entityId);
        bool hasTerrainBodies(uint64_t entityId) const;

        // Vegetation static capsule colliders per tile
        void addVegetationTileColliders(int32_t tileX, int32_t tileZ,
                                         const std::vector<JPH::BodyID>& bodyIds);
        JPH::BodyID addStaticCapsule(const glm::vec3& position, float yRotation, float scale,
                                      float radius, float height, uint8_t collisionLayer = 0);
        void removeVegetationTileColliders(int32_t tileX, int32_t tileZ);
        void removeAllVegetationColliders();

        void setContactAddedCallback(ContactCallback callback);
        void setContactRemovedCallback(ContactCallback callback);

        bool createRagdoll(uint64_t entityId, const RagdollBuildResult& buildResult);
        void destroyRagdoll(uint64_t entityId);
        bool hasRagdoll(uint64_t entityId) const;
        void activateRagdoll(uint64_t entityId);
        void deactivateRagdoll(uint64_t entityId);
        bool getRagdollPose(uint64_t entityId, JPH::SkeletonPose& outPose) const;
        void applyRagdollImpulse(uint64_t entityId, const glm::vec3& impulse);
        void applyRagdollBoneImpulse(uint64_t entityId, int physicsBoneIndex, const glm::vec3& impulse);
        bool createKinematicBoneBodies(uint64_t entityId, const RagdollBuildResult& buildResult,
                                        const glm::vec3& entityPosition = glm::vec3(0.0f));
        void destroyKinematicBoneBodies(uint64_t entityId);
        void updateKinematicBonePoses(uint64_t entityId,
                                       const std::vector<glm::mat4>& boneWorldTransforms,
                                       const std::vector<int>& physicsToAnimBoneIndex,
                                       float deltaTime);

        void transitionToRagdoll(uint64_t entityId, const JPH::SkeletonPose& currentPose);
        void transitionToKinematic(uint64_t entityId, const RagdollBuildResult& buildResult,
                                    const glm::vec3& entityPosition);

        bool addCharacter(uint64_t entityId, const CharacterCreateInfo& info);
        void removeCharacter(uint64_t entityId);
        bool hasCharacter(uint64_t entityId) const;
        CharacterUpdateResult updateCharacter(uint64_t entityId, const glm::vec3& desiredVelocity,
                                               float deltaTime, const glm::vec3& gravity);
        bool isCharacterGrounded(uint64_t entityId) const;
        glm::vec3 getCharacterPosition(uint64_t entityId) const;
        glm::vec3 getCharacterLinearVelocity(uint64_t entityId) const;
        void setCharacterPosition(uint64_t entityId, const glm::vec3& position);

    };
}
