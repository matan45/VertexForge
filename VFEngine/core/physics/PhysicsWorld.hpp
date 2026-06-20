#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include "JoltEnkiJobSystem.hpp"
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Skeleton/SkeletonPose.h>
#include "PhysicsContext.hpp"
#include "PhysicsBodyRegistry.hpp"
#include "PhysicsLayers.hpp"
#include "PhysicsContactListener.hpp"
#include "PhysicsRigidBodyManager.hpp"
#include "PhysicsTerrainManager.hpp"
#include "PhysicsRagdollManager.hpp"
#include "PhysicsCharacterManager.hpp"
#include "PhysicsStateBuffer.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>
#include <cstdint>

namespace core::physics
{
    struct RagdollBuildResult;

    class PhysicsWorld
    {
    private:
        std::unique_ptr<JoltEnkiJobSystem> jobSystem;
        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JPH::PhysicsSystem> physicsSystem;

        std::unique_ptr<BroadPhaseLayerInterfaceImpl> broadPhaseLayerInterface;
        std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> objectVsBroadPhaseFilter;
        std::unique_ptr<ObjectLayerPairFilterImpl> objectLayerPairFilter;

        std::unique_ptr<PhysicsContactListener> contactListener;

        PhysicsContext context;
        PhysicsBodyRegistry bodyRegistry;

        PhysicsRigidBodyManager rigidBodyManager;
        PhysicsTerrainManager terrainManager;
        PhysicsRagdollManager ragdollManager;
        PhysicsCharacterManager characterManager;
        PhysicsStateBuffer stateBuffer;

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

        void captureState();
        void swapStateBuffers();
        const PhysicsStateBuffer& getStateBuffer() const { return stateBuffer; }

        void setGravity(const glm::vec3& gravity);
        glm::vec3 getGravity() const;

        // Rigid body management
        JPH::BodyID addRigidBody(uint64_t entityId, const RigidBodyCreateInfo& bodyInfo,
                                 const ColliderCreateInfo& colliderInfo);
        void removeRigidBody(JPH::BodyID bodyId);
        void removeRigidBodyByEntity(uint64_t entityId);
        bool hasEntityBody(uint64_t entityId) const;

        // Position & rotation
        glm::vec3 getPosition(JPH::BodyID bodyId) const;
        glm::quat getRotation(JPH::BodyID bodyId) const;
        void setPosition(JPH::BodyID bodyId, const glm::vec3& position);
        void setRotation(JPH::BodyID bodyId, const glm::quat& rotation);

        // Velocity
        void setLinearVelocity(JPH::BodyID bodyId, const glm::vec3& velocity);
        glm::vec3 getLinearVelocity(JPH::BodyID bodyId) const;
        void setAngularVelocity(JPH::BodyID bodyId, const glm::vec3& velocity);
        glm::vec3 getAngularVelocity(JPH::BodyID bodyId) const;

        // Body state
        bool isBodyActive(JPH::BodyID bodyId) const;

        // Body properties
        BodyType getBodyType(JPH::BodyID bodyId) const;
        float getMass(JPH::BodyID bodyId) const;
        float getLinearDamping(JPH::BodyID bodyId) const;
        float getAngularDamping(JPH::BodyID bodyId) const;

        void setCollisionMatrix(const std::array<std::bitset<MAX_COLLISION_LAYERS>, MAX_COLLISION_LAYERS>& matrix);

        // Forces & impulses
        void applyForce(JPH::BodyID bodyId, const glm::vec3& force);
        void applyForceAtPosition(JPH::BodyID bodyId, const glm::vec3& force,
                                  const glm::vec3& position);
        void applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse);
        void applyTorque(JPH::BodyID bodyId, const glm::vec3& torque);

        // Raycasting
        RaycastResult raycast(const glm::vec3& origin, const glm::vec3& direction,
                              float maxDistance, uint16_t layerMask = 0xFFFF) const;
        std::vector<RaycastResult> raycastAll(const glm::vec3& origin, const glm::vec3& direction,
                                              float maxDistance, uint16_t layerMask = 0xFFFF) const;

        // Spatial overlap queries — return de-duplicated entity ids of overlapping bodies.
        std::vector<uint64_t> overlapSphere(const glm::vec3& center, float radius,
                                            uint16_t layerMask = 0xFFFF) const;
        std::vector<uint64_t> overlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
                                         const glm::quat& rotation, uint16_t layerMask = 0xFFFF) const;
        std::vector<uint64_t> overlapCapsule(const glm::vec3& center, float halfHeight, float radius,
                                             const glm::quat& rotation, uint16_t layerMask = 0xFFFF) const;

        bool areBodiesInContact(JPH::BodyID bodyA, JPH::BodyID bodyB) const;

        // Entity-body mapping
        JPH::BodyID getBodyForEntity(uint64_t entityId) const;
        uint64_t getEntityForBody(JPH::BodyID bodyId) const;

        // Terrain
        PhysicsTerrainManager& getTerrainManager() { return terrainManager; }
        const PhysicsTerrainManager& getTerrainManager() const { return terrainManager; }

        JPH::BodyID addTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                        const TerrainHeightFieldCreateInfo& info);
        void removeTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ);
        void removeAllTerrainBodies(uint64_t entityId);
        bool hasTerrainBodies(uint64_t entityId) const;

        // Cave
        void addCaveTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                              const services::CaveTileColliderInfo& cave);
        void removeCaveTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ);

        // Vegetation
        void addVegetationTileColliders(int32_t tileX, int32_t tileZ,
                                         const std::vector<JPH::BodyID>& bodyIds);
        JPH::BodyID addStaticCapsule(const glm::vec3& position, float yRotation, float scale,
                                      float radius, float height, uint8_t collisionLayer = 0);
        void removeVegetationTileColliders(int32_t tileX, int32_t tileZ);
        void removeAllVegetationColliders();

        // Contact callbacks
        void setContactAddedCallback(ContactCallback callback);
        void setContactRemovedCallback(ContactCallback callback);

        // Ragdoll
        bool createRagdoll(uint64_t entityId, const RagdollBuildResult& buildResult);
        void destroyRagdoll(uint64_t entityId);
        bool hasRagdoll(uint64_t entityId) const;
        void activateRagdoll(uint64_t entityId);
        void deactivateRagdoll(uint64_t entityId);
        bool getRagdollPose(uint64_t entityId, JPH::SkeletonPose& outPose) const;
        void applyRagdollImpulse(uint64_t entityId, const glm::vec3& impulse);
        void applyRagdollBoneImpulse(uint64_t entityId, int physicsBoneIndex, const glm::vec3& impulse);
        void driveRagdollToPose(uint64_t entityId, const JPH::SkeletonPose& targetPose,
                                const std::vector<float>& perBoneStrength,
                                const std::vector<float>& perBoneMaxTorque);
        void driveRagdollRoot(uint64_t entityId, const JPH::SkeletonPose& targetPose,
                              float strength, float deltaTime);
        void setRagdollMotorsOff(uint64_t entityId);
        bool isRagdollBelowVelocityThreshold(uint64_t entityId, float linearThreshold,
                                             float angularThreshold) const;
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

        // Character controller
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
