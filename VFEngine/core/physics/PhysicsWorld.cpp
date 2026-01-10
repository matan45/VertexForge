#include "PhysicsWorld.hpp"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include "PhysicsMeshLoader.hpp"
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
        return true; // Return true to break into debugger
    }
#endif

    PhysicsWorld::PhysicsWorld() = default;

    PhysicsWorld::~PhysicsWorld()
    {
        if (initialized)
        {
            cleanUp();
        }
    }

    bool PhysicsWorld::init()
    {
        if (initialized)
        {
            return true;
        }

        JPH::RegisterDefaultAllocator();
        JPH::Trace = JoltTraceImpl;

#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = JoltAssertFailedImpl;
#endif

        // NOTE: Raw new/delete is required here because JPH::Factory::sInstance is a global
        // static raw pointer that Jolt's type registration system (RegisterTypes/UnregisterTypes)
        // depends on. Jolt's API expects direct assignment to this static member. The factory
        // lifetime is managed manually: created before RegisterTypes() and destroyed after
        // UnregisterTypes() in cleanUp(). This follows Jolt's official initialization pattern.
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
        jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            numThreads
        );

        broadPhaseLayerInterface = std::make_unique<BroadPhaseLayerInterfaceImpl>();
        objectVsBroadPhaseFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
        objectLayerPairFilter = std::make_unique<ObjectLayerPairFilterImpl>();

        physicsSystem = std::make_unique<JPH::PhysicsSystem>();

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

        contactListener = std::make_unique<PhysicsContactListener>();
        physicsSystem->SetContactListener(contactListener.get());
        physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

        initialized = true;
        loggerInfo("Physics system initialized with {} threads", numThreads);
        return true;
    }

    void PhysicsWorld::cleanUp()
    {
        if (!initialized)
        {
            return;
        }

        auto& bodyInterface = physicsSystem->GetBodyInterface();
        for (auto& [entityId, bodyId] : entityToBody)
        {
            if (bodyInterface.IsAdded(bodyId))
            {
                bodyInterface.RemoveBody(bodyId);
            }
            bodyInterface.DestroyBody(bodyId);
        }
        entityToBody.clear();
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
        if (!initialized || !physicsSystem)
        {
            return;
        }

        physicsSystem->Update(deltaTime, collisionSteps, tempAllocator.get(), jobSystem.get());
    }

    void PhysicsWorld::processContactEvents()
    {
        if (contactListener)
        {
            contactListener->processContactEvents();
        }
    }

    void PhysicsWorld::setGravity(const glm::vec3& gravity)
    {
        if (physicsSystem)
        {
            physicsSystem->SetGravity(toJolt(gravity));
        }
    }

    glm::vec3 PhysicsWorld::getGravity() const
    {
        if (physicsSystem)
        {
            return toGlm(physicsSystem->GetGravity());
        }
        return glm::vec3(0.0f, -9.81f, 0.0f);
    }

    JPH::BodyID PhysicsWorld::addRigidBody(uint64_t entityId, const RigidBodyCreateInfo& bodyInfo,
                                           const ColliderCreateInfo& colliderInfo)
    {
        if (!initialized || !physicsSystem)
        {
            return JPH::BodyID();
        }

        JPH::Ref<JPH::Shape> shape = createShape(colliderInfo);
        if (!shape)
        {
            return JPH::BodyID();
        }

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
        JPH::ObjectLayer layer = static_cast<JPH::ObjectLayer>(clampedLayer);
        JPH::EMotionType motionType = getMotionType(bodyInfo.type);

        JPH::BodyCreationSettings settings(
            shape,
            toJoltR(bodyInfo.position),
            toJolt(bodyInfo.rotation),
            motionType,
            layer
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

        auto& bodyInterface = physicsSystem->GetBodyInterface();
        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
            settings,
            JPH::EActivation::Activate
        );

        if (!bodyId.IsInvalid())
        {
            entityToBody[entityId] = bodyId;
            bodyToEntity[bodyId.GetIndex()] = entityId;
        }

        return bodyId;
    }

    void PhysicsWorld::removeRigidBody(JPH::BodyID bodyId)
    {
        if (!initialized || !physicsSystem || bodyId.IsInvalid())
        {
            return;
        }

        auto& bodyInterface = physicsSystem->GetBodyInterface();

        auto it = bodyToEntity.find(bodyId.GetIndex());
        if (it != bodyToEntity.end())
        {
            entityToBody.erase(it->second);
            bodyToEntity.erase(it);
        }

        if (bodyInterface.IsAdded(bodyId))
        {
            bodyInterface.RemoveBody(bodyId);
        }
        bodyInterface.DestroyBody(bodyId);
    }

    void PhysicsWorld::removeRigidBodyByEntity(uint64_t entityId)
    {
        auto it = entityToBody.find(entityId);
        if (it != entityToBody.end())
        {
            removeRigidBody(it->second);
        }
    }

    bool PhysicsWorld::hasEntityBody(uint64_t entityId) const
    {
        return entityToBody.find(entityId) != entityToBody.end();
    }

    glm::vec3 PhysicsWorld::getPosition(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return glm::vec3(0.0f);
        }
        return toGlmR(physicsSystem->GetBodyInterface().GetPosition(bodyId));
    }

    glm::quat PhysicsWorld::getRotation(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        return toGlm(physicsSystem->GetBodyInterface().GetRotation(bodyId));
    }

    void PhysicsWorld::setPosition(JPH::BodyID bodyId, const glm::vec3& position)
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return;
        }
        physicsSystem->GetBodyInterface().SetPosition(
            bodyId,
            toJoltR(position),
            JPH::EActivation::Activate
        );
    }

    void PhysicsWorld::setRotation(JPH::BodyID bodyId, const glm::quat& rotation)
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return;
        }
        physicsSystem->GetBodyInterface().SetRotation(
            bodyId,
            toJolt(rotation),
            JPH::EActivation::Activate
        );
    }

    void PhysicsWorld::setLinearVelocity(JPH::BodyID bodyId, const glm::vec3& velocity)
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return;
        }
        physicsSystem->GetBodyInterface().SetLinearVelocity(bodyId, toJolt(velocity));
    }

    glm::vec3 PhysicsWorld::getLinearVelocity(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return glm::vec3(0.0f);
        }
        return toGlm(physicsSystem->GetBodyInterface().GetLinearVelocity(bodyId));
    }

    void PhysicsWorld::setAngularVelocity(JPH::BodyID bodyId, const glm::vec3& velocity)
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return;
        }
        physicsSystem->GetBodyInterface().SetAngularVelocity(bodyId, toJolt(velocity));
    }

    glm::vec3 PhysicsWorld::getAngularVelocity(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return glm::vec3(0.0f);
        }
        return toGlm(physicsSystem->GetBodyInterface().GetAngularVelocity(bodyId));
    }

    BodyType PhysicsWorld::getBodyType(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return BodyType::Static;
        }
        auto motionType = physicsSystem->GetBodyInterface().GetMotionType(bodyId);
        switch (motionType)
        {
        case JPH::EMotionType::Static:
            return BodyType::Static;
        case JPH::EMotionType::Kinematic:
            return BodyType::Kinematic;
        case JPH::EMotionType::Dynamic:
        default:
            return BodyType::Dynamic;
        }
    }

    float PhysicsWorld::getMass(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return 0.0f;
        }
        JPH::BodyLockRead lock(physicsSystem->GetBodyLockInterface(), bodyId);
        if (lock.Succeeded())
        {
            const JPH::Body& body = lock.GetBody();
            if (body.GetMotionProperties())
            {
                float inverseMass = body.GetMotionProperties()->GetInverseMass();
                return inverseMass > 0.0f ? 1.0f / inverseMass : 0.0f;
            }
        }
        return 0.0f;
    }

    float PhysicsWorld::getLinearDamping(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return 0.05f;
        }
        JPH::BodyLockRead lock(physicsSystem->GetBodyLockInterface(), bodyId);
        if (lock.Succeeded())
        {
            const JPH::Body& body = lock.GetBody();
            if (body.GetMotionProperties())
            {
                return body.GetMotionProperties()->GetLinearDamping();
            }
        }
        return 0.05f;
    }

    float PhysicsWorld::getAngularDamping(JPH::BodyID bodyId) const
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return 0.05f;
        }
        JPH::BodyLockRead lock(physicsSystem->GetBodyLockInterface(), bodyId);
        if (lock.Succeeded())
        {
            const JPH::Body& body = lock.GetBody();
            if (body.GetMotionProperties())
            {
                return body.GetMotionProperties()->GetAngularDamping();
            }
        }
        return 0.05f;
    }

    void PhysicsWorld::setCollisionMatrix(
        const std::array<std::bitset<MAX_COLLISION_LAYERS>, MAX_COLLISION_LAYERS>& matrix)
    {
        if (objectLayerPairFilter)
        {
            objectLayerPairFilter->setCollisionMatrix(matrix);
        }
    }

    void PhysicsWorld::applyForce(JPH::BodyID bodyId, const glm::vec3& force)
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return;
        }
        physicsSystem->GetBodyInterface().AddForce(bodyId, toJolt(force));
    }

    void PhysicsWorld::applyForceAtPosition(JPH::BodyID bodyId, const glm::vec3& force,
                                            const glm::vec3& position)
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return;
        }
        physicsSystem->GetBodyInterface().AddForce(bodyId, toJolt(force), toJoltR(position));
    }

    void PhysicsWorld::applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse)
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return;
        }
        physicsSystem->GetBodyInterface().AddImpulse(bodyId, toJolt(impulse));
    }

    void PhysicsWorld::applyTorque(JPH::BodyID bodyId, const glm::vec3& torque)
    {
        if (!physicsSystem || bodyId.IsInvalid())
        {
            return;
        }
        physicsSystem->GetBodyInterface().AddTorque(bodyId, toJolt(torque));
    }

    RaycastResult PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                        float maxDistance) const
    {
        RaycastResult result;

        if (!physicsSystem)
        {
            return result;
        }

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
            {
                result.entityId = it->second;
            }

            JPH::BodyLockRead lock(physicsSystem->GetBodyLockInterface(), hit.mBodyID);
            if (lock.Succeeded())
            {
                const JPH::Body& body = lock.GetBody();
                JPH::Vec3 surfaceNormal = body.GetWorldSpaceSurfaceNormal(
                    hit.mSubShapeID2,
                    ray.GetPointOnRay(hit.mFraction)
                );
                result.normal = toGlm(surfaceNormal);
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
        if (!physicsSystem || bodyA.IsInvalid() || bodyB.IsInvalid())
        {
            return false;
        }
        return physicsSystem->WereBodiesInContact(bodyA, bodyB);
    }

    JPH::BodyID PhysicsWorld::getBodyForEntity(uint64_t entityId) const
    {
        auto it = entityToBody.find(entityId);
        if (it != entityToBody.end())
        {
            return it->second;
        }
        return JPH::BodyID();
    }

    uint64_t PhysicsWorld::getEntityForBody(JPH::BodyID bodyId) const
    {
        if (bodyId.IsInvalid())
        {
            return 0;
        }
        auto it = bodyToEntity.find(bodyId.GetIndex());
        if (it != bodyToEntity.end())
        {
            return it->second;
        }
        return 0;
    }

    void PhysicsWorld::setContactAddedCallback(ContactCallback callback)
    {
        if (contactListener)
        {
            contactListener->setOnContactAdded(std::move(callback));
        }
    }

    void PhysicsWorld::setContactRemovedCallback(ContactCallback callback)
    {
        if (contactListener)
        {
            contactListener->setOnContactRemoved(std::move(callback));
        }
    }

    JPH::Ref<JPH::Shape> PhysicsWorld::createShape(const ColliderCreateInfo& info)
    {
        constexpr float MIN_DIMENSION = 0.001f;

        switch (info.shape)
        {
        case ColliderShape::Box:
            {
                glm::vec3 safeExtents = glm::max(info.halfExtents, glm::vec3(MIN_DIMENSION));
                if (safeExtents != info.halfExtents)
                {
                    loggerWarning("Box collider half-extents clamped from ({}, {}, {}) to ({}, {}, {})",
                                  info.halfExtents.x, info.halfExtents.y, info.halfExtents.z,
                                  safeExtents.x, safeExtents.y, safeExtents.z);
                }
                return new JPH::BoxShape(toJolt(safeExtents));
            }

        case ColliderShape::Sphere:
            {
                float safeRadius = std::max(info.radius, MIN_DIMENSION);
                if (safeRadius != info.radius)
                {
                    loggerWarning("Sphere collider radius clamped from {} to {}", info.radius, safeRadius);
                }
                return new JPH::SphereShape(safeRadius);
            }

        case ColliderShape::Capsule:
            {
                float safeRadius = std::max(info.radius, MIN_DIMENSION);
                // Jolt capsule uses half-height of the cylindrical part (not including hemispheres)
                // Total height = 2 * halfHeight + 2 * radius, so halfHeight = (height - 2*radius) / 2
                // Minimum half-height must be >= 0 (can be 0 for a sphere-like shape)
                float halfHeight = std::max(0.0f, info.height * 0.5f - safeRadius);
                if (safeRadius != info.radius || halfHeight != (info.height * 0.5f - info.radius))
                {
                    loggerWarning("Capsule collider adjusted: radius {} -> {}, halfHeight {} (from height {})",
                                  info.radius, safeRadius, halfHeight, info.height);
                }
                return new JPH::CapsuleShape(halfHeight, safeRadius);
            }

        case ColliderShape::ConvexMesh:
            {
                if (info.meshPath.empty())
                {
                    loggerWarning("ConvexMesh collider has no mesh path, using box fallback");
                    glm::vec3 safeExtents = glm::max(info.halfExtents, glm::vec3(MIN_DIMENSION));
                    return new JPH::BoxShape(toJolt(safeExtents));
                }

                auto meshData = PhysicsMeshLoader::loadAllSubmeshes(info.meshPath, 2);
                if (!meshData || meshData->vertices.empty())
                {
                    loggerWarning("ConvexMesh collider failed to load mesh: {}, using box fallback", info.meshPath);
                    glm::vec3 safeExtents = glm::max(info.halfExtents, glm::vec3(MIN_DIMENSION));
                    return new JPH::BoxShape(toJolt(safeExtents));
                }

                JPH::Array<JPH::Vec3> joltVertices;
                joltVertices.reserve(meshData->vertices.size());
                for (const auto& v : meshData->vertices)
                {
                    joltVertices.push_back(JPH::Vec3(v.x, v.y, v.z));
                }

                JPH::ConvexHullShapeSettings settings(joltVertices.data(), static_cast<int>(joltVertices.size()));
                settings.mMaxConvexRadius = 0.05f; // Small convex radius for better fit

                auto result = settings.Create();
                if (result.HasError())
                {
                    loggerWarning("ConvexMesh collider creation failed: {}, using box fallback",
                                  result.GetError().c_str());
                    glm::vec3 safeExtents = glm::max(info.halfExtents, glm::vec3(MIN_DIMENSION));
                    return new JPH::BoxShape(toJolt(safeExtents));
                }

                loggerInfo("Created ConvexMesh collider with {} vertices from: {}",
                           meshData->vertices.size(), info.meshPath);
                return result.Get();
            }

        case ColliderShape::TriangleMesh:
            {
                if (info.meshPath.empty())
                {
                    loggerWarning("TriangleMesh collider has no mesh path, using box fallback");
                    glm::vec3 safeExtents = glm::max(info.halfExtents, glm::vec3(MIN_DIMENSION));
                    return new JPH::BoxShape(toJolt(safeExtents));
                }

                auto meshData = PhysicsMeshLoader::loadAllSubmeshes(info.meshPath, 2);
                if (!meshData || meshData->vertices.empty() || meshData->indices.empty())
                {
                    loggerWarning("TriangleMesh collider failed to load mesh: {}, using box fallback", info.meshPath);
                    glm::vec3 safeExtents = glm::max(info.halfExtents, glm::vec3(MIN_DIMENSION));
                    return new JPH::BoxShape(toJolt(safeExtents));
                }

                JPH::TriangleList triangles;
                triangles.reserve(meshData->indices.size() / 3);

                for (size_t i = 0; i + 2 < meshData->indices.size(); i += 3)
                {
                    const auto& v0 = meshData->vertices[meshData->indices[i]];
                    const auto& v1 = meshData->vertices[meshData->indices[i + 1]];
                    const auto& v2 = meshData->vertices[meshData->indices[i + 2]];

                    triangles.push_back(JPH::Triangle(
                        JPH::Float3(v0.x, v0.y, v0.z),
                        JPH::Float3(v1.x, v1.y, v1.z),
                        JPH::Float3(v2.x, v2.y, v2.z)
                    ));
                }

                JPH::MeshShapeSettings settings(triangles);

                auto result = settings.Create();
                if (result.HasError())
                {
                    loggerWarning("TriangleMesh collider creation failed: {}, using box fallback",
                                  result.GetError().c_str());
                    glm::vec3 safeExtents = glm::max(info.halfExtents, glm::vec3(MIN_DIMENSION));
                    return new JPH::BoxShape(toJolt(safeExtents));
                }

                loggerInfo("Created TriangleMesh collider with {} triangles from: {}",
                           triangles.size(), info.meshPath);
                return result.Get();
            }

        default:
            loggerWarning("Unknown collider shape type {}, defaulting to unit box", static_cast<int>(info.shape));
            return new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        }
    }

    JPH::ObjectLayer PhysicsWorld::getObjectLayer(BodyType type, bool isTrigger)
    {
        if (isTrigger)
        {
            return Layers::SENSOR;
        }

        switch (type)
        {
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

    JPH::EMotionType PhysicsWorld::getMotionType(BodyType type)
    {
        switch (type)
        {
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

    JPH::Vec3 PhysicsWorld::toJolt(const glm::vec3& v)
    {
        return JPH::Vec3(v.x, v.y, v.z);
    }

    JPH::Quat PhysicsWorld::toJolt(const glm::quat& q)
    {
        glm::quat normalized = glm::normalize(q);
        if (glm::any(glm::isnan(normalized)))
        {
            return JPH::Quat::sIdentity();
        }
        return JPH::Quat(normalized.x, normalized.y, normalized.z, normalized.w);
    }

    JPH::RVec3 PhysicsWorld::toJoltR(const glm::vec3& v)
    {
        return JPH::RVec3(v.x, v.y, v.z);
    }

    glm::vec3 PhysicsWorld::toGlm(const JPH::Vec3& v)
    {
        return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
    }

    glm::vec3 PhysicsWorld::toGlmR(const JPH::RVec3& v)
    {
        return glm::vec3(
            static_cast<float>(v.GetX()),
            static_cast<float>(v.GetY()),
            static_cast<float>(v.GetZ())
        );
    }

    glm::quat PhysicsWorld::toGlm(const JPH::Quat& q)
    {
        return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }
}
