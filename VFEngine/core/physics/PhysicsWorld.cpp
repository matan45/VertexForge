#include "PhysicsWorld.hpp"
#include "PhysicsShapeFactory.hpp"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include "print/Logger.hpp"

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
        loggerInfo("Jolt: {}", buffer);
    }

#ifdef JPH_ENABLE_ASSERTS
    static bool JoltAssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile,
                                     unsigned int inLine)
    {
        loggerError("Jolt Assertion Failed: {} - {} ({}:{})", inExpression, inMessage ? inMessage : "", inFile, inLine);
        return true;
    }
#endif

    namespace
    {
        void removeAndDestroyBody(JPH::BodyInterface& bi, JPH::BodyID bodyId)
        {
            if (bi.IsAdded(bodyId)) bi.RemoveBody(bodyId);
            bi.DestroyBody(bodyId);
        }

        template<typename Func>
        float getMotionProp(const JPH::PhysicsSystem* ps, JPH::BodyID bodyId,
                            Func&& getter, float defaultVal)
        {
            if (!ps || bodyId.IsInvalid()) return defaultVal;
            JPH::BodyLockRead lock(ps->GetBodyLockInterface(), bodyId);
            if (lock.Succeeded())
            {
                const auto* mp = lock.GetBody().GetMotionProperties();
                if (mp) return getter(mp);
            }
            return defaultVal;
        }

        JPH::BodyCreationSettings buildRigidBodySettings(
            const JPH::Ref<JPH::Shape>& shape, const RigidBodyCreateInfo& bodyInfo,
            const ColliderCreateInfo& colliderInfo, uint64_t entityId)
        {
            uint8_t clampedLayer;
            if (colliderInfo.collisionLayer < MAX_COLLISION_LAYERS)
            {
                clampedLayer = colliderInfo.collisionLayer;
            }
            else
            {
                clampedLayer = static_cast<uint8_t>(Layers::DYNAMIC);
                loggerWarning("Entity {}: Invalid collision layer {} (max: {}), defaulting to DYNAMIC ({})",
                              entityId, colliderInfo.collisionLayer, MAX_COLLISION_LAYERS - 1, clampedLayer);
            }

            JPH::BodyCreationSettings settings(
                shape,
                toJoltR(bodyInfo.position),
                toJolt(bodyInfo.rotation),
                PhysicsShapeFactory::getMotionType(bodyInfo.type),
                static_cast<JPH::ObjectLayer>(clampedLayer)
            );

            settings.mLinearVelocity = toJolt(bodyInfo.linearVelocity);
            settings.mAngularVelocity = toJolt(bodyInfo.angularVelocity);
            settings.mFriction = bodyInfo.friction;
            settings.mRestitution = bodyInfo.restitution;
            settings.mLinearDamping = bodyInfo.linearDamping;
            settings.mAngularDamping = bodyInfo.angularDamping;
            settings.mGravityFactor = 1.0f;
            settings.mIsSensor = colliderInfo.isTrigger;
            settings.mUserData = entityId;

            if (bodyInfo.type == BodyType::Dynamic)
            {
                if (bodyInfo.mass > 0.0f)
                {
                    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                    settings.mMassPropertiesOverride.mMass = bodyInfo.mass;
                }
                else
                {
                    loggerWarning("Entity {}: Invalid mass {} for dynamic body, using shape-calculated mass",
                                  entityId, bodyInfo.mass);
                }
            }

            return settings;
        }
    }

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

        // NOTE: Raw new/delete is required here because JPH::Factory::sInstance is a global
        // static raw pointer that Jolt's type registration system depends on.
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
        jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

        broadPhaseLayerInterface = std::make_unique<BroadPhaseLayerInterfaceImpl>();
        objectVsBroadPhaseFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
        objectLayerPairFilter = std::make_unique<ObjectLayerPairFilterImpl>();

        physicsSystem = std::make_unique<JPH::PhysicsSystem>();
        physicsSystem->Init(10240, 0, 65536, 10240,
            *broadPhaseLayerInterface, *objectVsBroadPhaseFilter, *objectLayerPairFilter);

        contactListener = std::make_unique<PhysicsContactListener>();
        physicsSystem->SetContactListener(contactListener.get());
        physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

        initialized = true;
        loggerInfo("Physics system initialized with {} threads", numThreads);
        return true;
    }

    void PhysicsWorld::cleanUp()
    {
        if (!initialized) return;

        auto& bodyInterface = physicsSystem->GetBodyInterface();

        for (auto& [entityId, ragdollData] : entityRagdolls)
        {
            if (ragdollData.ragdoll)
            {
                ragdollData.ragdoll->RemoveFromPhysicsSystem();
            }
        }
        entityRagdolls.clear();

        for (auto& [entityId, boneBodies] : entityBoneBodies)
        {
            for (auto& bodyId : boneBodies)
            {
                removeAndDestroyBody(bodyInterface, bodyId);
            }
        }
        entityBoneBodies.clear();
        bodyToBoneIndex.clear();

        entityCharacters.clear();

        for (auto& [entityId, bodyId] : entityToBody)
            removeAndDestroyBody(bodyInterface, bodyId);
        entityToBody.clear();

        for (auto& [entityId, tileMap] : terrainBodies)
            for (auto& [tileKey, bodyId] : tileMap)
                removeAndDestroyBody(bodyInterface, bodyId);
        terrainBodies.clear();
        bodyToEntity.clear();

        contactListener.reset();
        physicsSystem.reset();
        objectLayerPairFilter.reset();
        objectVsBroadPhaseFilter.reset();
        broadPhaseLayerInterface.reset();
        jobSystem.reset();
        tempAllocator.reset();

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

    void PhysicsWorld::setGravity(const glm::vec3& gravity)
    {
        if (physicsSystem) physicsSystem->SetGravity(toJolt(gravity));
    }

    glm::vec3 PhysicsWorld::getGravity() const
    {
        if (physicsSystem) return toGlm(physicsSystem->GetGravity());
        return glm::vec3(0.0f, -9.81f, 0.0f);
    }

    JPH::BodyID PhysicsWorld::addRigidBody(uint64_t entityId, const RigidBodyCreateInfo& bodyInfo,
                                           const ColliderCreateInfo& colliderInfo)
    {
        if (!initialized || !physicsSystem) return JPH::BodyID();

        JPH::Ref<JPH::Shape> shape = PhysicsShapeFactory::createShape(colliderInfo);
        if (!shape) return JPH::BodyID();

        if (colliderInfo.offset.x != 0.0f || colliderInfo.offset.y != 0.0f || colliderInfo.offset.z != 0.0f)
        {
            shape = new JPH::RotatedTranslatedShape(
                toJolt(colliderInfo.offset), JPH::Quat::sIdentity(), shape);
        }

        auto settings = buildRigidBodySettings(shape, bodyInfo, colliderInfo, entityId);

        auto& bodyInterface = physicsSystem->GetBodyInterface();
        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);

        if (!bodyId.IsInvalid())
        {
            entityToBody[entityId] = bodyId;
            bodyToEntity[bodyId.GetIndex()] = entityId;
        }

        return bodyId;
    }

    void PhysicsWorld::removeRigidBody(JPH::BodyID bodyId)
    {
        if (!initialized || !physicsSystem || bodyId.IsInvalid()) return;

        auto& bodyInterface = physicsSystem->GetBodyInterface();

        auto it = bodyToEntity.find(bodyId.GetIndex());
        if (it != bodyToEntity.end())
        {
            entityToBody.erase(it->second);
            bodyToEntity.erase(it);
        }

        removeAndDestroyBody(bodyInterface, bodyId);
    }

    void PhysicsWorld::removeRigidBodyByEntity(uint64_t entityId)
    {
        destroyKinematicBoneBodies(entityId);
        destroyRagdoll(entityId);

        auto it = entityToBody.find(entityId);
        if (it != entityToBody.end()) removeRigidBody(it->second);
    }

    bool PhysicsWorld::hasEntityBody(uint64_t entityId) const
    {
        return entityToBody.find(entityId) != entityToBody.end();
    }

    glm::vec3 PhysicsWorld::getPosition(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid()) return glm::vec3(0.0f);
        return toGlmR(physicsSystem->GetBodyInterface().GetPosition(bodyId));
    }

    glm::quat PhysicsWorld::getRotation(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return toGlm(physicsSystem->GetBodyInterface().GetRotation(bodyId));
    }

    void PhysicsWorld::setPosition(JPH::BodyID bodyId, const glm::vec3& position)
    {
        if (!physicsSystem || bodyId.IsInvalid()) return;
        physicsSystem->GetBodyInterface().SetPosition(bodyId, toJoltR(position), JPH::EActivation::Activate);
    }

    void PhysicsWorld::setRotation(JPH::BodyID bodyId, const glm::quat& rotation)
    {
        if (!physicsSystem || bodyId.IsInvalid()) return;
        physicsSystem->GetBodyInterface().SetRotation(bodyId, toJolt(rotation), JPH::EActivation::Activate);
    }

    void PhysicsWorld::setLinearVelocity(JPH::BodyID bodyId, const glm::vec3& velocity)
    {
        if (!physicsSystem || bodyId.IsInvalid()) return;
        physicsSystem->GetBodyInterface().SetLinearVelocity(bodyId, toJolt(velocity));
    }

    glm::vec3 PhysicsWorld::getLinearVelocity(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid()) return glm::vec3(0.0f);
        return toGlm(physicsSystem->GetBodyInterface().GetLinearVelocity(bodyId));
    }

    void PhysicsWorld::setAngularVelocity(JPH::BodyID bodyId, const glm::vec3& velocity)
    {
        if (!physicsSystem || bodyId.IsInvalid()) return;
        physicsSystem->GetBodyInterface().SetAngularVelocity(bodyId, toJolt(velocity));
    }

    glm::vec3 PhysicsWorld::getAngularVelocity(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid()) return glm::vec3(0.0f);
        return toGlm(physicsSystem->GetBodyInterface().GetAngularVelocity(bodyId));
    }

    BodyType PhysicsWorld::getBodyType(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid()) return BodyType::Static;
        auto motionType = physicsSystem->GetBodyInterface().GetMotionType(bodyId);
        switch (motionType)
        {
        case JPH::EMotionType::Static: return BodyType::Static;
        case JPH::EMotionType::Kinematic: return BodyType::Kinematic;
        case JPH::EMotionType::Dynamic:
        default: return BodyType::Dynamic;
        }
    }

    float PhysicsWorld::getMass(JPH::BodyID bodyId) const
    {
        return getMotionProp(physicsSystem.get(), bodyId, [](const JPH::MotionProperties* mp) {
            float inv = mp->GetInverseMass();
            return inv > 0.0f ? 1.0f / inv : 0.0f;
        }, 0.0f);
    }

    float PhysicsWorld::getLinearDamping(JPH::BodyID bodyId) const
    {
        return getMotionProp(physicsSystem.get(), bodyId,
            [](const auto* mp) { return mp->GetLinearDamping(); }, 0.05f);
    }

    float PhysicsWorld::getAngularDamping(JPH::BodyID bodyId) const
    {
        return getMotionProp(physicsSystem.get(), bodyId,
            [](const auto* mp) { return mp->GetAngularDamping(); }, 0.05f);
    }

    void PhysicsWorld::setCollisionMatrix(
        const std::array<std::bitset<MAX_COLLISION_LAYERS>, MAX_COLLISION_LAYERS>& matrix)
    {
        if (objectLayerPairFilter) objectLayerPairFilter->setCollisionMatrix(matrix);
    }

    void PhysicsWorld::applyForce(JPH::BodyID bodyId, const glm::vec3& force)
    {
        if (!physicsSystem || bodyId.IsInvalid()) return;
        physicsSystem->GetBodyInterface().AddForce(bodyId, toJolt(force));
    }

    void PhysicsWorld::applyForceAtPosition(JPH::BodyID bodyId, const glm::vec3& force,
                                            const glm::vec3& position)
    {
        if (!physicsSystem || bodyId.IsInvalid()) return;
        physicsSystem->GetBodyInterface().AddForce(bodyId, toJolt(force), toJoltR(position));
    }

    void PhysicsWorld::applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse)
    {
        if (!physicsSystem || bodyId.IsInvalid()) return;
        physicsSystem->GetBodyInterface().AddImpulse(bodyId, toJolt(impulse));
    }

    void PhysicsWorld::applyTorque(JPH::BodyID bodyId, const glm::vec3& torque)
    {
        if (!physicsSystem || bodyId.IsInvalid()) return;
        physicsSystem->GetBodyInterface().AddTorque(bodyId, toJolt(torque));
    }

    RaycastResult PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                        float maxDistance) const
    {
        RaycastResult result;
        if (!physicsSystem) return result;

        glm::vec3 normalizedDir = glm::normalize(direction);
        JPH::RRayCast ray(toJoltR(origin), toJolt(normalizedDir * maxDistance));
        JPH::RayCastResult hit;

        if (physicsSystem->GetNarrowPhaseQuery().CastRay(ray, hit))
        {
            result.hit = true;
            result.distance = hit.mFraction * maxDistance;
            result.point = origin + normalizedDir * result.distance;

            auto it = bodyToEntity.find(hit.mBodyID.GetIndex());
            if (it != bodyToEntity.end())
                result.entityId = it->second;

            JPH::BodyLockRead lock(physicsSystem->GetBodyLockInterface(), hit.mBodyID);
            if (lock.Succeeded())
            {
                result.normal = toGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(
                    hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
            }
            else
            {
                result.normal = -normalizedDir;
            }
        }

        return result;
    }

    bool PhysicsWorld::areBodiesInContact(JPH::BodyID bodyA, JPH::BodyID bodyB) const
    {
        if (!physicsSystem || bodyA.IsInvalid() || bodyB.IsInvalid()) return false;
        return physicsSystem->WereBodiesInContact(bodyA, bodyB);
    }

    JPH::BodyID PhysicsWorld::getBodyForEntity(uint64_t entityId) const
    {
        auto it = entityToBody.find(entityId);
        return it != entityToBody.end() ? it->second : JPH::BodyID();
    }

    uint64_t PhysicsWorld::getEntityForBody(JPH::BodyID bodyId) const
    {
        if (bodyId.IsInvalid()) return 0;
        auto it = bodyToEntity.find(bodyId.GetIndex());
        return it != bodyToEntity.end() ? it->second : 0;
    }

    void PhysicsWorld::setContactAddedCallback(ContactCallback callback)
    {
        if (contactListener) contactListener->setOnContactAdded(std::move(callback));
    }

    void PhysicsWorld::setContactRemovedCallback(ContactCallback callback)
    {
        if (contactListener) contactListener->setOnContactRemoved(std::move(callback));
    }

    PhysicsWorld::TileCoordKey PhysicsWorld::makeTileKey(int32_t x, int32_t z)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32)
             | static_cast<uint64_t>(static_cast<uint32_t>(z));
    }

    JPH::BodyID PhysicsWorld::addTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                                  const TerrainHeightFieldCreateInfo& info)
    {
        if (!initialized || !physicsSystem || !info.heightSamples || info.sampleCount == 0)
            return JPH::BodyID();

        JPH::HeightFieldShapeSettings shapeSettings(
            info.heightSamples,
            JPH::Vec3(info.offset.x, info.offset.y, info.offset.z),
            JPH::Vec3(info.scale.x, info.scale.y, info.scale.z),
            info.sampleCount);

        auto shapeResult = shapeSettings.Create();
        if (!shapeResult.IsValid())
        {
            loggerError("Failed to create HeightFieldShape for terrain tile ({}, {}): {}",
                        tileX, tileZ, shapeResult.GetError().c_str());
            return JPH::BodyID();
        }

        JPH::BodyCreationSettings bodySettings(
            shapeResult.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
            JPH::EMotionType::Static, static_cast<JPH::ObjectLayer>(info.collisionLayer));

        bodySettings.mFriction = info.friction;
        bodySettings.mRestitution = info.restitution;
        bodySettings.mUserData = entityId;

        auto& bodyInterface = physicsSystem->GetBodyInterface();
        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);

        if (!bodyId.IsInvalid())
        {
            terrainBodies[entityId][makeTileKey(tileX, tileZ)] = bodyId;
            bodyToEntity[bodyId.GetIndex()] = entityId;
        }

        return bodyId;
    }

    void PhysicsWorld::removeTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ)
    {
        if (!initialized || !physicsSystem) return;

        auto entityIt = terrainBodies.find(entityId);
        if (entityIt == terrainBodies.end()) return;

        auto tileIt = entityIt->second.find(makeTileKey(tileX, tileZ));
        if (tileIt == entityIt->second.end()) return;

        JPH::BodyID bodyId = tileIt->second;
        bodyToEntity.erase(bodyId.GetIndex());
        removeAndDestroyBody(physicsSystem->GetBodyInterface(), bodyId);

        entityIt->second.erase(tileIt);
        if (entityIt->second.empty()) terrainBodies.erase(entityIt);
    }

    void PhysicsWorld::removeAllTerrainBodies(uint64_t entityId)
    {
        if (!initialized || !physicsSystem) return;

        auto entityIt = terrainBodies.find(entityId);
        if (entityIt == terrainBodies.end()) return;

        auto& bodyInterface = physicsSystem->GetBodyInterface();
        for (auto& [tileKey, bodyId] : entityIt->second)
        {
            bodyToEntity.erase(bodyId.GetIndex());
            removeAndDestroyBody(bodyInterface, bodyId);
        }

        terrainBodies.erase(entityIt);
    }

    bool PhysicsWorld::hasTerrainBodies(uint64_t entityId) const
    {
        auto it = terrainBodies.find(entityId);
        return it != terrainBodies.end() && !it->second.empty();
    }

    // ============================================
    // Character Controller (CharacterVirtual)
    // ============================================

    bool PhysicsWorld::addCharacter(uint64_t entityId, const CharacterCreateInfo& info)
    {
        if (!initialized || !physicsSystem) return false;
        if (entityCharacters.count(entityId)) return false;

        // Build shape based on collider type
        JPH::RefConst<JPH::Shape> characterShape;
        switch (info.shape)
        {
        case types::ColliderShape::Sphere:
            characterShape = new JPH::SphereShape(info.size.x);
            break;
        case types::ColliderShape::Box:
            characterShape = new JPH::BoxShape(JPH::Vec3(info.size.x, info.size.y, info.size.z));
            break;
        case types::ColliderShape::Capsule:
        default:
        {
            float radius = info.size.x;
            float totalHeight = info.size.y;
            float cylinderHalfHeight = (totalHeight * 0.5f) - radius;
            if (cylinderHalfHeight < 0.01f) cylinderHalfHeight = 0.01f;
            characterShape = new JPH::CapsuleShape(cylinderHalfHeight, radius);
            break;
        }
        }

        JPH::CharacterVirtualSettings settings;
        settings.mShape = characterShape;
        settings.mMaxSlopeAngle = JPH::DegreesToRadians(info.maxSlopeAngle);
        settings.mMaxStrength = 100.0f;
        settings.mMass = 70.0f;
        settings.mPredictiveContactDistance = 0.1f;
        settings.mPenetrationRecoverySpeed = 1.0f;
        settings.mEnhancedInternalEdgeRemoval = true;

        auto character = new JPH::CharacterVirtual(
            &settings,
            toJoltR(info.position),
            toJolt(info.rotation),
            entityId,
            physicsSystem.get()
        );

        entityCharacters[entityId] = character;

        loggerInfo("Character controller created for entity {} at ({:.1f}, {:.1f}, {:.1f})",
                   entityId, info.position.x, info.position.y, info.position.z);
        return true;
    }

    void PhysicsWorld::removeCharacter(uint64_t entityId)
    {
        entityCharacters.erase(entityId);
    }

    bool PhysicsWorld::hasCharacter(uint64_t entityId) const
    {
        return entityCharacters.count(entityId) > 0;
    }

    CharacterUpdateResult PhysicsWorld::updateCharacter(uint64_t entityId,
                                                         const glm::vec3& desiredVelocity,
                                                         float deltaTime,
                                                         const glm::vec3& gravity)
    {
        CharacterUpdateResult result;
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end() || !physicsSystem) return result;

        auto* character = it->second.GetPtr();

        // Set the desired velocity (gravity is applied by caller)
        character->SetLinearVelocity(toJolt(desiredVelocity));

        // Configure ExtendedUpdate settings for stair stepping and floor sticking
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -0.5f, 0.0f);
        updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, 0.4f, 0.0f);

        // Use default accept-all filters for character collision queries
        JPH::BroadPhaseLayerFilter broadPhaseFilter;
        JPH::ObjectLayerFilter objectLayerFilter;
        JPH::BodyFilter bodyFilter;
        JPH::ShapeFilter shapeFilter;

        character->ExtendedUpdate(
            deltaTime,
            toJolt(gravity),
            updateSettings,
            broadPhaseFilter,
            objectLayerFilter,
            bodyFilter,
            shapeFilter,
            *tempAllocator
        );

        // Read back results
        result.position = toGlmR(character->GetPosition());
        result.linearVelocity = toGlm(character->GetLinearVelocity());
        result.groundNormal = toGlm(character->GetGroundNormal());
        result.groundVelocity = toGlm(character->GetGroundVelocity());

        auto groundState = character->GetGroundState();
        result.isGrounded = (groundState == JPH::CharacterBase::EGroundState::OnGround);

        return result;
    }

    bool PhysicsWorld::isCharacterGrounded(uint64_t entityId) const
    {
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end()) return false;
        return it->second->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

    glm::vec3 PhysicsWorld::getCharacterPosition(uint64_t entityId) const
    {
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end()) return glm::vec3(0.0f);
        return toGlmR(it->second->GetPosition());
    }

    glm::vec3 PhysicsWorld::getCharacterLinearVelocity(uint64_t entityId) const
    {
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end()) return glm::vec3(0.0f);
        return toGlm(it->second->GetLinearVelocity());
    }

    void PhysicsWorld::setCharacterPosition(uint64_t entityId, const glm::vec3& position)
    {
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end()) return;
        it->second->SetPosition(toJoltR(position));
    }

    void PhysicsWorld::setCharacterLinearVelocity(uint64_t entityId, const glm::vec3& velocity)
    {
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end()) return;
        it->second->SetLinearVelocity(toJolt(velocity));
    }

}
