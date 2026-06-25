#include "PhysicsWorld.hpp"
#include "JoltConversions.hpp"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include "print/Log.hpp"

#include <thread>
#include <cstdarg>

namespace core::physics
{
    static void JoltTraceImpl(const char* inFMT, ...)
    {
        va_list list;
        va_start(list, inFMT);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), inFMT, list);
        va_end(list);
        vfLogInfo("Jolt: {}", buffer);
    }

#ifdef JPH_ENABLE_ASSERTS
    static bool JoltAssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile,
                                     unsigned int inLine)
    {
        vfLogError("Jolt Assertion Failed: {} - {} ({}:{})", inExpression, inMessage ? inMessage : "", inFile, inLine);
        return true;
    }
#endif

    PhysicsWorld::PhysicsWorld() = default;

    PhysicsWorld::~PhysicsWorld()
    {
        if (initialized) cleanUp();
    }

    bool PhysicsWorld::init()
    {
        if (initialized) return true;

        JPH::RegisterDefaultAllocator();
        JPH::Trace = JoltTraceImpl;

#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = JoltAssertFailedImpl;
#endif

        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        // Run Jolt's internal jobs on the engine's shared enkiTS pool rather than a
        // second dedicated thread pool (avoids core oversubscription during the step).
        jobSystem = std::make_unique<JoltEnkiJobSystem>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers);

        broadPhaseLayerInterface = std::make_unique<BroadPhaseLayerInterfaceImpl>();
        objectVsBroadPhaseFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
        objectLayerPairFilter = std::make_unique<ObjectLayerPairFilterImpl>();

        physicsSystem = std::make_unique<JPH::PhysicsSystem>();
        physicsSystem->Init(10240, 0, 65536, 10240,
            *broadPhaseLayerInterface, *objectVsBroadPhaseFilter, *objectLayerPairFilter);

        contactListener = std::make_unique<PhysicsContactListener>();
        physicsSystem->SetContactListener(contactListener.get());
        physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

        // Populate shared context
        context.physicsSystem = physicsSystem.get();
        context.tempAllocator = tempAllocator.get();
        context.broadPhaseFilter = objectVsBroadPhaseFilter.get();
        context.objectLayerFilter = objectLayerPairFilter.get();

        // Initialize managers
        rigidBodyManager.init(&context, &bodyRegistry);
        terrainManager.init(&context);
        ragdollManager.init(&context, &bodyRegistry);
        characterManager.init(&context);

        initialized = true;
        vfLogInfo("Physics system initialized (Jolt jobs on shared enkiTS pool, max concurrency {})",
                  jobSystem->GetMaxConcurrency());
        return true;
    }

    void PhysicsWorld::cleanUp()
    {
        if (!initialized) return;

        ragdollManager.cleanUp();
        characterManager.cleanUp();
        terrainManager.cleanUp();

        // Clean up remaining rigid bodies
        auto& bodyInterface = physicsSystem->GetBodyInterface();
        for (auto& [entityId, bodyId] : bodyRegistry.getAllEntityBodies())
            removeAndDestroyBody(bodyInterface, bodyId);
        bodyRegistry.clear();
        bodyRegistry.clearBoneIndices();

        contactListener.reset();
        physicsSystem.reset();
        objectLayerPairFilter.reset();
        objectVsBroadPhaseFilter.reset();
        broadPhaseLayerInterface.reset();
        jobSystem.reset();
        tempAllocator.reset();

        context = PhysicsContext{};

        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        initialized = false;
    }

    void PhysicsWorld::step(float deltaTime, int collisionSteps)
    {
        if (!initialized || !physicsSystem) return;
        physicsSystem->Update(deltaTime, collisionSteps, tempAllocator.get(), jobSystem.get());
    }

    void PhysicsWorld::processContactEvents()
    {
        if (contactListener) contactListener->processContactEvents();
    }

    void PhysicsWorld::captureState()
    {
        if (!initialized || !physicsSystem) return;
        stateBuffer.capture(bodyRegistry, *physicsSystem);
    }

    void PhysicsWorld::swapStateBuffers()
    {
        stateBuffer.swap();
    }

    void PhysicsWorld::setGravity(const glm::vec3& gravity)
    {
        if (physicsSystem) physicsSystem->SetGravity(toJolt(gravity));
    }

    glm::vec3 PhysicsWorld::getGravity() const
    {
        if (physicsSystem) return toGlm(physicsSystem->GetGravity());
        return glm::vec3(0.0f, -9.81f, 0.0f);
    }

    void PhysicsWorld::setCollisionMatrix(
        const std::array<std::bitset<MAX_COLLISION_LAYERS>, MAX_COLLISION_LAYERS>& matrix)
    {
        if (objectLayerPairFilter) objectLayerPairFilter->setCollisionMatrix(matrix);
    }

    void PhysicsWorld::setContactAddedCallback(ContactCallback callback)
    {
        if (contactListener) contactListener->setOnContactAdded(std::move(callback));
    }

    void PhysicsWorld::setContactRemovedCallback(ContactCallback callback)
    {
        if (contactListener) contactListener->setOnContactRemoved(std::move(callback));
    }

    // Entity-body mapping
    JPH::BodyID PhysicsWorld::getBodyForEntity(uint64_t entityId) const
    {
        return bodyRegistry.getBodyForEntity(entityId);
    }

    uint64_t PhysicsWorld::getEntityForBody(JPH::BodyID bodyId) const
    {
        return bodyRegistry.getEntityForBody(bodyId);
    }

    bool PhysicsWorld::hasEntityBody(uint64_t entityId) const
    {
        return bodyRegistry.hasEntity(entityId);
    }

    // Rigid body forwarding
    JPH::BodyID PhysicsWorld::addRigidBody(uint64_t entityId, const RigidBodyCreateInfo& bodyInfo,
                                           const ColliderCreateInfo& colliderInfo)
    {
        return rigidBodyManager.addRigidBody(entityId, bodyInfo, colliderInfo);
    }

    void PhysicsWorld::removeRigidBody(JPH::BodyID bodyId)
    {
        rigidBodyManager.removeRigidBody(bodyId);
    }

    void PhysicsWorld::removeRigidBodyByEntity(uint64_t entityId)
    {
        ragdollManager.destroyKinematicBoneBodies(entityId);
        ragdollManager.destroyRagdoll(entityId);

        auto bodyId = bodyRegistry.getBodyForEntity(entityId);
        if (!bodyId.IsInvalid()) rigidBodyManager.removeRigidBody(bodyId);
    }

    void PhysicsWorld::setEntityRigidBodyEnabled(uint64_t entityId, bool enabled)
    {
        JPH::BodyID bodyId = bodyRegistry.getBodyForEntity(entityId);
        if (!bodyId.IsInvalid()) rigidBodyManager.setBodyEnabled(bodyId, enabled);
    }

    glm::vec3 PhysicsWorld::getPosition(JPH::BodyID bodyId) const { return rigidBodyManager.getPosition(bodyId); }
    glm::quat PhysicsWorld::getRotation(JPH::BodyID bodyId) const { return rigidBodyManager.getRotation(bodyId); }
    void PhysicsWorld::setPosition(JPH::BodyID bodyId, const glm::vec3& position) { rigidBodyManager.setPosition(bodyId, position); }
    void PhysicsWorld::setRotation(JPH::BodyID bodyId, const glm::quat& rotation) { rigidBodyManager.setRotation(bodyId, rotation); }

    void PhysicsWorld::setLinearVelocity(JPH::BodyID bodyId, const glm::vec3& velocity) { rigidBodyManager.setLinearVelocity(bodyId, velocity); }
    glm::vec3 PhysicsWorld::getLinearVelocity(JPH::BodyID bodyId) const { return rigidBodyManager.getLinearVelocity(bodyId); }
    void PhysicsWorld::setAngularVelocity(JPH::BodyID bodyId, const glm::vec3& velocity) { rigidBodyManager.setAngularVelocity(bodyId, velocity); }
    glm::vec3 PhysicsWorld::getAngularVelocity(JPH::BodyID bodyId) const { return rigidBodyManager.getAngularVelocity(bodyId); }

    bool PhysicsWorld::isBodyActive(JPH::BodyID bodyId) const { return rigidBodyManager.isBodyActive(bodyId); }

    BodyType PhysicsWorld::getBodyType(JPH::BodyID bodyId) const { return rigidBodyManager.getBodyType(bodyId); }
    float PhysicsWorld::getMass(JPH::BodyID bodyId) const { return rigidBodyManager.getMass(bodyId); }
    float PhysicsWorld::getLinearDamping(JPH::BodyID bodyId) const { return rigidBodyManager.getLinearDamping(bodyId); }
    float PhysicsWorld::getAngularDamping(JPH::BodyID bodyId) const { return rigidBodyManager.getAngularDamping(bodyId); }

    void PhysicsWorld::applyForce(JPH::BodyID bodyId, const glm::vec3& force) { rigidBodyManager.applyForce(bodyId, force); }
    void PhysicsWorld::applyForceAtPosition(JPH::BodyID bodyId, const glm::vec3& force, const glm::vec3& position) { rigidBodyManager.applyForceAtPosition(bodyId, force, position); }
    void PhysicsWorld::applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse) { rigidBodyManager.applyImpulse(bodyId, impulse); }
    void PhysicsWorld::applyTorque(JPH::BodyID bodyId, const glm::vec3& torque) { rigidBodyManager.applyTorque(bodyId, torque); }

    RaycastResult PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                        float maxDistance, uint16_t layerMask) const
    {
        return rigidBodyManager.raycast(origin, direction, maxDistance, layerMask);
    }

    std::vector<RaycastResult> PhysicsWorld::raycastAll(const glm::vec3& origin, const glm::vec3& direction,
                                                         float maxDistance, uint16_t layerMask) const
    {
        return rigidBodyManager.raycastAll(origin, direction, maxDistance, layerMask);
    }

    std::vector<uint64_t> PhysicsWorld::overlapSphere(const glm::vec3& center, float radius,
                                                       uint16_t layerMask) const
    {
        return rigidBodyManager.overlapSphere(center, radius, layerMask);
    }

    std::vector<uint64_t> PhysicsWorld::overlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
                                                    const glm::quat& rotation, uint16_t layerMask) const
    {
        return rigidBodyManager.overlapBox(center, halfExtents, rotation, layerMask);
    }

    std::vector<uint64_t> PhysicsWorld::overlapCapsule(const glm::vec3& center, float halfHeight,
                                                        float radius, const glm::quat& rotation,
                                                        uint16_t layerMask) const
    {
        return rigidBodyManager.overlapCapsule(center, halfHeight, radius, rotation, layerMask);
    }

    bool PhysicsWorld::areBodiesInContact(JPH::BodyID bodyA, JPH::BodyID bodyB) const
    {
        return rigidBodyManager.areBodiesInContact(bodyA, bodyB);
    }

    // Terrain forwarding
    JPH::BodyID PhysicsWorld::addTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                                  const TerrainHeightFieldCreateInfo& info)
    {
        return terrainManager.addTerrainTileBody(entityId, tileX, tileZ, info);
    }

    void PhysicsWorld::removeTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ)
    {
        terrainManager.removeTerrainTileBody(entityId, tileX, tileZ);
    }

    void PhysicsWorld::removeAllTerrainBodies(uint64_t entityId) { terrainManager.removeAllTerrainBodies(entityId); }
    bool PhysicsWorld::hasTerrainBodies(uint64_t entityId) const { return terrainManager.hasTerrainBodies(entityId); }

    void PhysicsWorld::addCaveTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                        const services::CaveTileColliderInfo& cave)
    {
        terrainManager.addCaveTileBody(entityId, tileX, tileZ, cave);
    }

    void PhysicsWorld::removeCaveTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ)
    {
        terrainManager.removeCaveTileBody(entityId, tileX, tileZ);
    }

    JPH::BodyID PhysicsWorld::addStaticCapsule(const glm::vec3& position, float yRotation, float scale,
                                                float radius, float height, uint8_t collisionLayer)
    {
        return terrainManager.addStaticCapsule(position, yRotation, scale, radius, height, collisionLayer);
    }

    void PhysicsWorld::addVegetationTileColliders(int32_t tileX, int32_t tileZ, const std::vector<JPH::BodyID>& bodyIds)
    {
        terrainManager.addVegetationTileColliders(tileX, tileZ, bodyIds);
    }

    void PhysicsWorld::removeVegetationTileColliders(int32_t tileX, int32_t tileZ) { terrainManager.removeVegetationTileColliders(tileX, tileZ); }
    void PhysicsWorld::removeAllVegetationColliders() { terrainManager.removeAllVegetationColliders(); }

    // Ragdoll forwarding
    bool PhysicsWorld::createRagdoll(uint64_t entityId, const RagdollBuildResult& buildResult) { return ragdollManager.createRagdoll(entityId, buildResult); }
    void PhysicsWorld::destroyRagdoll(uint64_t entityId) { ragdollManager.destroyRagdoll(entityId); }
    bool PhysicsWorld::hasRagdoll(uint64_t entityId) const { return ragdollManager.hasRagdoll(entityId); }
    void PhysicsWorld::activateRagdoll(uint64_t entityId) { ragdollManager.activateRagdoll(entityId); }
    void PhysicsWorld::deactivateRagdoll(uint64_t entityId) { ragdollManager.deactivateRagdoll(entityId); }
    bool PhysicsWorld::getRagdollPose(uint64_t entityId, JPH::SkeletonPose& outPose) const { return ragdollManager.getRagdollPose(entityId, outPose); }
    void PhysicsWorld::applyRagdollImpulse(uint64_t entityId, const glm::vec3& impulse) { ragdollManager.applyRagdollImpulse(entityId, impulse); }
    void PhysicsWorld::applyRagdollBoneImpulse(uint64_t entityId, int physicsBoneIndex, const glm::vec3& impulse) { ragdollManager.applyRagdollBoneImpulse(entityId, physicsBoneIndex, impulse); }
    void PhysicsWorld::driveRagdollToPose(uint64_t entityId, const JPH::SkeletonPose& targetPose, const std::vector<float>& perBoneStrength, const std::vector<float>& perBoneMaxTorque) { ragdollManager.driveRagdollToPose(entityId, targetPose, perBoneStrength, perBoneMaxTorque); }
    void PhysicsWorld::driveRagdollRoot(uint64_t entityId, const JPH::SkeletonPose& targetPose, float strength, float deltaTime) { ragdollManager.driveRagdollRoot(entityId, targetPose, strength, deltaTime); }
    void PhysicsWorld::setRagdollMotorsOff(uint64_t entityId) { ragdollManager.setRagdollMotorsOff(entityId); }
    bool PhysicsWorld::isRagdollBelowVelocityThreshold(uint64_t entityId, float linearThreshold, float angularThreshold) const { return ragdollManager.isRagdollBelowVelocityThreshold(entityId, linearThreshold, angularThreshold); }

    bool PhysicsWorld::createKinematicBoneBodies(uint64_t entityId, const RagdollBuildResult& buildResult, const glm::vec3& entityPosition)
    {
        return ragdollManager.createKinematicBoneBodies(entityId, buildResult, entityPosition);
    }

    void PhysicsWorld::destroyKinematicBoneBodies(uint64_t entityId) { ragdollManager.destroyKinematicBoneBodies(entityId); }

    void PhysicsWorld::updateKinematicBonePoses(uint64_t entityId, const std::vector<glm::mat4>& boneWorldTransforms,
                                                 const std::vector<int>& physicsToAnimBoneIndex, float deltaTime)
    {
        ragdollManager.updateKinematicBonePoses(entityId, boneWorldTransforms, physicsToAnimBoneIndex, deltaTime);
    }

    void PhysicsWorld::transitionToRagdoll(uint64_t entityId, const JPH::SkeletonPose& currentPose) { ragdollManager.transitionToRagdoll(entityId, currentPose); }
    void PhysicsWorld::transitionToKinematic(uint64_t entityId, const RagdollBuildResult& buildResult, const glm::vec3& entityPosition) { ragdollManager.transitionToKinematic(entityId, buildResult, entityPosition); }

    // Character forwarding
    bool PhysicsWorld::addCharacter(uint64_t entityId, const CharacterCreateInfo& info) { return characterManager.addCharacter(entityId, info); }
    void PhysicsWorld::removeCharacter(uint64_t entityId) { characterManager.removeCharacter(entityId); }
    bool PhysicsWorld::hasCharacter(uint64_t entityId) const { return characterManager.hasCharacter(entityId); }

    CharacterUpdateResult PhysicsWorld::updateCharacter(uint64_t entityId, const glm::vec3& desiredVelocity,
                                                         float deltaTime, const glm::vec3& gravity)
    {
        return characterManager.updateCharacter(entityId, desiredVelocity, deltaTime, gravity);
    }

    bool PhysicsWorld::isCharacterGrounded(uint64_t entityId) const { return characterManager.isCharacterGrounded(entityId); }
    glm::vec3 PhysicsWorld::getCharacterPosition(uint64_t entityId) const { return characterManager.getCharacterPosition(entityId); }
    glm::vec3 PhysicsWorld::getCharacterLinearVelocity(uint64_t entityId) const { return characterManager.getCharacterLinearVelocity(entityId); }
    void PhysicsWorld::setCharacterPosition(uint64_t entityId, const glm::vec3& position) { characterManager.setCharacterPosition(entityId, position); }
}
