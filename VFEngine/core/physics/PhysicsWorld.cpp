#include "PhysicsWorld.hpp"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

#include <thread>

namespace core::physics {

    // Jolt memory allocation callbacks (using default allocator)
    static void* JoltAllocate(size_t inSize) {
        return malloc(inSize);
    }

    static void* JoltAlignedAllocate(size_t inSize, size_t inAlignment) {
        return _aligned_malloc(inSize, inAlignment);
    }

    static void JoltFree(void* inBlock) {
        free(inBlock);
    }

    static void JoltAlignedFree(void* inBlock) {
        _aligned_free(inBlock);
    }

    PhysicsWorld::PhysicsWorld() = default;

    PhysicsWorld::~PhysicsWorld() {
        if (initialized) {
            cleanUp();
        }
    }

    bool PhysicsWorld::init() {
        if (initialized) {
            return true;
        }

        // Register allocation hook
        JPH::Allocate = JoltAllocate;
        JPH::AlignedAllocate = JoltAlignedAllocate;
        JPH::Free = JoltFree;
        JPH::AlignedFree = JoltAlignedFree;

        // Create factory
        JPH::Factory::sInstance = new JPH::Factory();

        // Register physics types
        JPH::RegisterTypes();

        // Create temp allocator (10 MB)
        tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        // Create job system (use hardware threads - 1 for background work)
        int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
        jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            numThreads
        );

        // Create layer interfaces
        broadPhaseLayerInterface = std::make_unique<BroadPhaseLayerInterfaceImpl>();
        objectVsBroadPhaseFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
        objectLayerPairFilter = std::make_unique<ObjectLayerPairFilterImpl>();

        // Create physics system
        physicsSystem = std::make_unique<JPH::PhysicsSystem>();

        // Initialize physics system
        // Max 10240 bodies, auto-detect mutexes, 65536 body pairs, 10240 contacts
        constexpr uint32_t maxBodies = 10240;
        constexpr uint32_t numBodyMutexes = 0; // auto-detect
        constexpr uint32_t maxBodyPairs = 65536;
        constexpr uint32_t maxContactConstraints = 10240;

        physicsSystem->Init(
            maxBodies,
            numBodyMutexes,
            maxBodyPairs,
            maxContactConstraints,
            *broadPhaseLayerInterface,
            *objectVsBroadPhaseFilter,
            *objectLayerPairFilter
        );

        // Create and set contact listener
        contactListener = std::make_unique<PhysicsContactListener>();
        physicsSystem->SetContactListener(contactListener.get());

        // Set default gravity
        physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

        initialized = true;
        return true;
    }

    void PhysicsWorld::cleanUp() {
        if (!initialized) {
            return;
        }

        // Remove all bodies
        auto& bodyInterface = physicsSystem->GetBodyInterface();
        for (auto& [entityId, bodyId] : entityToBody) {
            if (bodyInterface.IsAdded(bodyId)) {
                bodyInterface.RemoveBody(bodyId);
            }
            bodyInterface.DestroyBody(bodyId);
        }
        entityToBody.clear();
        bodyToEntity.clear();

        // Clean up in reverse order
        contactListener.reset();
        physicsSystem.reset();
        objectLayerPairFilter.reset();
        objectVsBroadPhaseFilter.reset();
        broadPhaseLayerInterface.reset();
        jobSystem.reset();
        tempAllocator.reset();

        // Unregister types and destroy factory
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        initialized = false;
    }

    void PhysicsWorld::step(float deltaTime, int collisionSteps) {
        if (!initialized || !physicsSystem) {
            return;
        }

        physicsSystem->Update(deltaTime, collisionSteps, tempAllocator.get(), jobSystem.get());
    }

    void PhysicsWorld::processContactEvents() {
        if (contactListener) {
            contactListener->processContactEvents();
        }
    }

    void PhysicsWorld::setGravity(const glm::vec3& gravity) {
        if (physicsSystem) {
            physicsSystem->SetGravity(toJolt(gravity));
        }
    }

    glm::vec3 PhysicsWorld::getGravity() const {
        if (physicsSystem) {
            return toGlm(physicsSystem->GetGravity());
        }
        return glm::vec3(0.0f, -9.81f, 0.0f);
    }

    JPH::BodyID PhysicsWorld::addRigidBody(uint64_t entityId, const RigidBodyCreateInfo& bodyInfo,
        const ColliderCreateInfo& colliderInfo) {
        if (!initialized || !physicsSystem) {
            return JPH::BodyID();
        }

        // Create shape
        JPH::Ref<JPH::Shape> shape = createShape(colliderInfo);
        if (!shape) {
            return JPH::BodyID();
        }

        // Determine object layer
        JPH::ObjectLayer layer = getObjectLayer(bodyInfo.type, colliderInfo.isTrigger);
        JPH::EMotionType motionType = getMotionType(bodyInfo.type);

        // Create body settings
        JPH::BodyCreationSettings settings(
            shape,
            toJoltR(bodyInfo.position),
            toJolt(bodyInfo.rotation),
            motionType,
            layer
        );

        // Set body properties
        settings.mLinearVelocity = toJolt(bodyInfo.linearVelocity);
        settings.mAngularVelocity = toJolt(bodyInfo.angularVelocity);
        settings.mFriction = bodyInfo.friction;
        settings.mRestitution = bodyInfo.restitution;
        settings.mLinearDamping = bodyInfo.linearDamping;
        settings.mAngularDamping = bodyInfo.angularDamping;
        settings.mGravityFactor = bodyInfo.useGravity ? 1.0f : 0.0f;
        settings.mIsSensor = colliderInfo.isTrigger;
        settings.mUserData = entityId;

        // Set mass for dynamic bodies
        if (bodyInfo.type == BodyType::Dynamic && bodyInfo.mass > 0.0f) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = bodyInfo.mass;
        }

        // Create and add body
        auto& bodyInterface = physicsSystem->GetBodyInterface();
        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
            settings,
            JPH::EActivation::Activate
        );

        if (!bodyId.IsInvalid()) {
            entityToBody[entityId] = bodyId;
            bodyToEntity[bodyId.GetIndex()] = entityId;
        }

        return bodyId;
    }

    void PhysicsWorld::removeRigidBody(JPH::BodyID bodyId) {
        if (!initialized || !physicsSystem || bodyId.IsInvalid()) {
            return;
        }

        auto& bodyInterface = physicsSystem->GetBodyInterface();

        // Find and remove entity mapping
        auto it = bodyToEntity.find(bodyId.GetIndex());
        if (it != bodyToEntity.end()) {
            entityToBody.erase(it->second);
            bodyToEntity.erase(it);
        }

        // Remove and destroy body
        if (bodyInterface.IsAdded(bodyId)) {
            bodyInterface.RemoveBody(bodyId);
        }
        bodyInterface.DestroyBody(bodyId);
    }

    void PhysicsWorld::removeRigidBodyByEntity(uint64_t entityId) {
        auto it = entityToBody.find(entityId);
        if (it != entityToBody.end()) {
            removeRigidBody(it->second);
        }
    }

    bool PhysicsWorld::hasBody(JPH::BodyID bodyId) const {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return false;
        }
        return physicsSystem->GetBodyInterface().IsAdded(bodyId);
    }

    bool PhysicsWorld::hasEntityBody(uint64_t entityId) const {
        return entityToBody.find(entityId) != entityToBody.end();
    }

    glm::vec3 PhysicsWorld::getPosition(JPH::BodyID bodyId) const {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return glm::vec3(0.0f);
        }
        return toGlmR(physicsSystem->GetBodyInterface().GetPosition(bodyId));
    }

    glm::quat PhysicsWorld::getRotation(JPH::BodyID bodyId) const {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        return toGlm(physicsSystem->GetBodyInterface().GetRotation(bodyId));
    }

    void PhysicsWorld::setPosition(JPH::BodyID bodyId, const glm::vec3& position) {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return;
        }
        physicsSystem->GetBodyInterface().SetPosition(
            bodyId,
            toJoltR(position),
            JPH::EActivation::Activate
        );
    }

    void PhysicsWorld::setRotation(JPH::BodyID bodyId, const glm::quat& rotation) {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return;
        }
        physicsSystem->GetBodyInterface().SetRotation(
            bodyId,
            toJolt(rotation),
            JPH::EActivation::Activate
        );
    }

    void PhysicsWorld::setLinearVelocity(JPH::BodyID bodyId, const glm::vec3& velocity) {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return;
        }
        physicsSystem->GetBodyInterface().SetLinearVelocity(bodyId, toJolt(velocity));
    }

    glm::vec3 PhysicsWorld::getLinearVelocity(JPH::BodyID bodyId) const {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return glm::vec3(0.0f);
        }
        return toGlm(physicsSystem->GetBodyInterface().GetLinearVelocity(bodyId));
    }

    void PhysicsWorld::setAngularVelocity(JPH::BodyID bodyId, const glm::vec3& velocity) {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return;
        }
        physicsSystem->GetBodyInterface().SetAngularVelocity(bodyId, toJolt(velocity));
    }

    glm::vec3 PhysicsWorld::getAngularVelocity(JPH::BodyID bodyId) const {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return glm::vec3(0.0f);
        }
        return toGlm(physicsSystem->GetBodyInterface().GetAngularVelocity(bodyId));
    }

    void PhysicsWorld::applyForce(JPH::BodyID bodyId, const glm::vec3& force) {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return;
        }
        physicsSystem->GetBodyInterface().AddForce(bodyId, toJolt(force));
    }

    void PhysicsWorld::applyForceAtPosition(JPH::BodyID bodyId, const glm::vec3& force,
        const glm::vec3& position) {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return;
        }
        physicsSystem->GetBodyInterface().AddForce(bodyId, toJolt(force), toJoltR(position));
    }

    void PhysicsWorld::applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse) {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return;
        }
        physicsSystem->GetBodyInterface().AddImpulse(bodyId, toJolt(impulse));
    }

    void PhysicsWorld::applyTorque(JPH::BodyID bodyId, const glm::vec3& torque) {
        if (!physicsSystem || bodyId.IsInvalid()) {
            return;
        }
        physicsSystem->GetBodyInterface().AddTorque(bodyId, toJolt(torque));
    }

    RaycastResult PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
        float maxDistance) const {
        RaycastResult result;

        if (!physicsSystem) {
            return result;
        }

        JPH::RRayCast ray(toJoltR(origin), toJolt(glm::normalize(direction) * maxDistance));
        JPH::RayCastResult hit;

        if (physicsSystem->GetNarrowPhaseQuery().CastRay(ray, hit)) {
            result.hit = true;
            result.distance = hit.mFraction * maxDistance;
            result.point = origin + glm::normalize(direction) * result.distance;

            // Get entity ID from body
            auto it = bodyToEntity.find(hit.mBodyID.GetIndex());
            if (it != bodyToEntity.end()) {
                result.entityId = it->second;
            }

            // Get normal (would need additional query for accurate normal)
            result.normal = -glm::normalize(direction);  // Approximate
        }

        return result;
    }

    std::vector<RaycastResult> PhysicsWorld::raycastAll(const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance) const {
        std::vector<RaycastResult> results;

        if (!physicsSystem) {
            return results;
        }

        JPH::RayCast ray(toJolt(origin), toJolt(glm::normalize(direction) * maxDistance));
        JPH::AllHitCollisionCollector<JPH::RayCastBodyCollector> collector;

        physicsSystem->GetBroadPhaseQuery().CastRay(ray, collector, {}, {});

        for (const auto& hit : collector.mHits) {
            RaycastResult result;
            result.hit = true;
            result.distance = hit.mFraction * maxDistance;
            result.point = origin + glm::normalize(direction) * result.distance;

            auto it = bodyToEntity.find(hit.mBodyID.GetIndex());
            if (it != bodyToEntity.end()) {
                result.entityId = it->second;
            }

            result.normal = -glm::normalize(direction);
            results.push_back(result);
        }

        return results;
    }

    JPH::BodyID PhysicsWorld::getBodyForEntity(uint64_t entityId) const {
        auto it = entityToBody.find(entityId);
        if (it != entityToBody.end()) {
            return it->second;
        }
        return JPH::BodyID();
    }

    uint64_t PhysicsWorld::getEntityForBody(JPH::BodyID bodyId) const {
        if (bodyId.IsInvalid()) {
            return 0;
        }
        auto it = bodyToEntity.find(bodyId.GetIndex());
        if (it != bodyToEntity.end()) {
            return it->second;
        }
        return 0;
    }

    void PhysicsWorld::setContactAddedCallback(ContactCallback callback) {
        if (contactListener) {
            contactListener->setOnContactAdded(std::move(callback));
        }
    }

    void PhysicsWorld::setContactRemovedCallback(ContactCallback callback) {
        if (contactListener) {
            contactListener->setOnContactRemoved(std::move(callback));
        }
    }

    JPH::Ref<JPH::Shape> PhysicsWorld::createShape(const ColliderCreateInfo& info) {
        switch (info.shape) {
        case ColliderShape::Box:
            return new JPH::BoxShape(toJolt(info.halfExtents));

        case ColliderShape::Sphere:
            return new JPH::SphereShape(info.radius);

        case ColliderShape::Capsule:
            // Jolt capsule uses half-height, not full height
            return new JPH::CapsuleShape(info.height * 0.5f - info.radius, info.radius);

        default:
            return new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        }
    }

    JPH::ObjectLayer PhysicsWorld::getObjectLayer(BodyType type, bool isTrigger) {
        if (isTrigger) {
            return Layers::SENSOR;
        }

        switch (type) {
        case BodyType::Static:
            return Layers::STATIC;
        case BodyType::Dynamic:
            return Layers::DYNAMIC;
        case BodyType::Kinematic:
            return Layers::KINEMATIC;
        default:
            return Layers::DYNAMIC;
        }
    }

    JPH::EMotionType PhysicsWorld::getMotionType(BodyType type) {
        switch (type) {
        case BodyType::Static:
            return JPH::EMotionType::Static;
        case BodyType::Dynamic:
            return JPH::EMotionType::Dynamic;
        case BodyType::Kinematic:
            return JPH::EMotionType::Kinematic;
        default:
            return JPH::EMotionType::Dynamic;
        }
    }

    // Type conversion helpers
    JPH::Vec3 PhysicsWorld::toJolt(const glm::vec3& v) {
        return JPH::Vec3(v.x, v.y, v.z);
    }

    JPH::Quat PhysicsWorld::toJolt(const glm::quat& q) {
        return JPH::Quat(q.x, q.y, q.z, q.w);
    }

    JPH::RVec3 PhysicsWorld::toJoltR(const glm::vec3& v) {
        return JPH::RVec3(v.x, v.y, v.z);
    }

    glm::vec3 PhysicsWorld::toGlm(const JPH::Vec3& v) {
        return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
    }

    glm::vec3 PhysicsWorld::toGlmR(const JPH::RVec3& v) {
        return glm::vec3(
            static_cast<float>(v.GetX()),
            static_cast<float>(v.GetY()),
            static_cast<float>(v.GetZ())
        );
    }

    glm::quat PhysicsWorld::toGlm(const JPH::Quat& q) {
        return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }

}
