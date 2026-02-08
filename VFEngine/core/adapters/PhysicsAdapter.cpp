#include "PhysicsAdapter.hpp"
#include "../../services/events/PhysicsEvents.hpp"
#include "../../services/events/EventDispatcher.hpp"

// =============================================================================
// Static assertions to ensure enum synchronization across namespaces
// =============================================================================

// BodyType enums: core::physics::BodyType <-> services::RigidBodyData::Type
static_assert(
    static_cast<int>(core::physics::BodyType::Static) ==
    static_cast<int>(services::RigidBodyData::Type::Static),
    "BodyType::Static mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::BodyType::Dynamic) ==
    static_cast<int>(services::RigidBodyData::Type::Dynamic),
    "BodyType::Dynamic mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::BodyType::Kinematic) ==
    static_cast<int>(services::RigidBodyData::Type::Kinematic),
    "BodyType::Kinematic mismatch between core::physics and services");

// ColliderShape enums: core::physics::ColliderShape <-> services::ColliderData::Shape
static_assert(
    static_cast<int>(core::physics::ColliderShape::Box) ==
    static_cast<int>(services::ColliderData::Shape::Box),
    "ColliderShape::Box mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::ColliderShape::Sphere) ==
    static_cast<int>(services::ColliderData::Shape::Sphere),
    "ColliderShape::Sphere mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::ColliderShape::Capsule) ==
    static_cast<int>(services::ColliderData::Shape::Capsule),
    "ColliderShape::Capsule mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::ColliderShape::ConvexMesh) ==
    static_cast<int>(services::ColliderData::Shape::ConvexMesh),
    "ColliderShape::ConvexMesh mismatch between core::physics and services");
static_assert(
    static_cast<int>(core::physics::ColliderShape::TriangleMesh) ==
    static_cast<int>(services::ColliderData::Shape::TriangleMesh),
    "ColliderShape::TriangleMesh mismatch between core::physics and services");

namespace core
{
    PhysicsAdapter::PhysicsAdapter()
        : physicsWorld(std::make_unique<physics::PhysicsWorld>())
          , fixedTimestep(std::make_unique<physics::FixedTimestep>())
          , currentSettings(types::PhysicsSettings::createDefault())
    {
    }

    PhysicsAdapter::~PhysicsAdapter()
    {
        cleanUp();
    }

    bool PhysicsAdapter::init()
    {
        if (!physicsWorld->init())
        {
            return false;
        }

        physicsWorld->setContactAddedCallback([this](const physics::ContactEvent& event)
        {
            auto& dispatcher = events::EventDispatcher::instance();

            // Map body IDs to entity handles
            uint64_t entityIdA = physicsWorld->getEntityForBody(event.bodyA);
            uint64_t entityIdB = physicsWorld->getEntityForBody(event.bodyB);

            if (entityIdA == 0 || entityIdB == 0)
            {
                return; // Skip if entities not found
            }

            services::EntityHandle entityA{entityIdA};
            services::EntityHandle entityB{entityIdB};

            if (event.isSensor)
            {
                // Publish trigger enter notification
                events::physics::TriggerEnterNotification notification;
                notification.triggerEntity = entityA;
                notification.otherEntity = entityB;
                dispatcher.publish(notification);
            }
            else
            {
                // Publish collision start notification
                events::physics::CollisionStartNotification notification;
                notification.entityA = entityA;
                notification.entityB = entityB;
                notification.contactPoint = event.contactPoint;
                notification.normal = event.normal;
                notification.penetrationDepth = event.penetrationDepth;
                dispatcher.publish(notification);
            }
        });

        physicsWorld->setContactRemovedCallback([this](const physics::ContactEvent& event)
        {
            auto& dispatcher = events::EventDispatcher::instance();

            // Map body IDs to entity handles
            uint64_t entityIdA = physicsWorld->getEntityForBody(event.bodyA);
            uint64_t entityIdB = physicsWorld->getEntityForBody(event.bodyB);

            if (entityIdA == 0 || entityIdB == 0)
            {
                return; // Skip if entities not found
            }

            services::EntityHandle entityA{entityIdA};
            services::EntityHandle entityB{entityIdB};

            if (event.isSensor)
            {
                // Publish trigger exit notification
                events::physics::TriggerExitNotification notification;
                notification.triggerEntity = entityA;
                notification.otherEntity = entityB;
                dispatcher.publish(notification);
            }
            else
            {
                // Publish collision end notification
                events::physics::CollisionEndNotification notification;
                notification.entityA = entityA;
                notification.entityB = entityB;
                dispatcher.publish(notification);
            }
        });

        return true;
    }

    void PhysicsAdapter::cleanUp()
    {
        if (physicsWorld)
        {
            physicsWorld->cleanUp();
        }
        fixedTimestep->reset();
    }

    bool PhysicsAdapter::isInitialized() const
    {
        return physicsWorld && physicsWorld->isInitialized();
    }

    void PhysicsAdapter::update(float deltaTime)
    {
        if (!isInitialized())
        {
            return;
        }

        fixedTimestep->update(deltaTime, [this](float fixedDt)
        {
            physicsWorld->step(fixedDt);
        });

        physicsWorld->processContactEvents();
    }

    void PhysicsAdapter::setGravity(const glm::vec3& gravity)
    {
        if (physicsWorld)
        {
            physicsWorld->setGravity(gravity);
        }
    }

    glm::vec3 PhysicsAdapter::getGravity() const
    {
        if (physicsWorld)
        {
            return physicsWorld->getGravity();
        }
        return glm::vec3(0.0f, -9.81f, 0.0f);
    }

    void PhysicsAdapter::addRigidBody(services::EntityHandle entity,
                                      const services::RigidBodyData& data,
                                      const services::ColliderData& collider)
    {
        if (!physicsWorld)
        {
            return;
        }

        auto bodyInfo = toPhysicsBodyInfo(data);
        auto colliderInfo = toPhysicsColliderInfo(collider);

        physicsWorld->addRigidBody(entity.id, bodyInfo, colliderInfo);
    }

    void PhysicsAdapter::removeRigidBody(services::EntityHandle entity)
    {
        if (physicsWorld)
        {
            physicsWorld->removeRigidBodyByEntity(entity.id);
        }
    }

    bool PhysicsAdapter::hasRigidBody(services::EntityHandle entity) const
    {
        if (physicsWorld)
        {
            return physicsWorld->hasEntityBody(entity.id);
        }
        return false;
    }

    std::optional<services::RigidBodyData> PhysicsAdapter::getRigidBody(
        services::EntityHandle entity) const
    {
        if (!physicsWorld || !physicsWorld->hasEntityBody(entity.id))
        {
            return std::nullopt;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (bodyId.IsInvalid())
        {
            return std::nullopt;
        }

        services::RigidBodyData data;

        auto bodyType = physicsWorld->getBodyType(bodyId);
        switch (bodyType)
        {
        case physics::BodyType::Static:
            data.type = services::RigidBodyData::Type::Static;
            break;
        case physics::BodyType::Kinematic:
            data.type = services::RigidBodyData::Type::Kinematic;
            break;
        case physics::BodyType::Dynamic:
        default:
            data.type = services::RigidBodyData::Type::Dynamic;
            break;
        }

        // Query all properties from Jolt
        data.mass = physicsWorld->getMass(bodyId);
        data.linearDamping = physicsWorld->getLinearDamping(bodyId);
        data.angularDamping = physicsWorld->getAngularDamping(bodyId);
        data.linearVelocity = physicsWorld->getLinearVelocity(bodyId);
        data.angularVelocity = physicsWorld->getAngularVelocity(bodyId);

        return data;
    }

    void PhysicsAdapter::addCollider(services::EntityHandle entity,
                                     const services::ColliderData& data)
    {
        // For standalone colliders (without rigid body), create a static body
        if (!physicsWorld)
        {
            return;
        }

        services::RigidBodyData bodyData;
        bodyData.type = services::RigidBodyData::Type::Static;

        addRigidBody(entity, bodyData, data);
    }

    void PhysicsAdapter::removeCollider(services::EntityHandle entity)
    {
        removeRigidBody(entity);
    }

    void PhysicsAdapter::applyForce(services::EntityHandle entity, const glm::vec3& force)
    {
        if (!physicsWorld)
        {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            physicsWorld->applyForce(bodyId, force);
        }
    }

    void PhysicsAdapter::applyForceAtPosition(services::EntityHandle entity,
                                              const glm::vec3& force,
                                              const glm::vec3& position)
    {
        if (!physicsWorld)
        {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            physicsWorld->applyForceAtPosition(bodyId, force, position);
        }
    }

    void PhysicsAdapter::applyImpulse(services::EntityHandle entity, const glm::vec3& impulse)
    {
        if (!physicsWorld)
        {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            physicsWorld->applyImpulse(bodyId, impulse);
        }
    }

    void PhysicsAdapter::applyTorque(services::EntityHandle entity, const glm::vec3& torque)
    {
        if (!physicsWorld)
        {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            physicsWorld->applyTorque(bodyId, torque);
        }
    }

    void PhysicsAdapter::setLinearVelocity(services::EntityHandle entity,
                                           const glm::vec3& velocity)
    {
        if (!physicsWorld)
        {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            physicsWorld->setLinearVelocity(bodyId, velocity);
        }
    }

    glm::vec3 PhysicsAdapter::getLinearVelocity(services::EntityHandle entity) const
    {
        if (!physicsWorld)
        {
            return glm::vec3(0.0f);
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            return physicsWorld->getLinearVelocity(bodyId);
        }
        return glm::vec3(0.0f);
    }

    void PhysicsAdapter::setAngularVelocity(services::EntityHandle entity,
                                            const glm::vec3& velocity)
    {
        if (!physicsWorld)
        {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            physicsWorld->setAngularVelocity(bodyId, velocity);
        }
    }

    glm::vec3 PhysicsAdapter::getAngularVelocity(services::EntityHandle entity) const
    {
        if (!physicsWorld)
        {
            return glm::vec3(0.0f);
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            return physicsWorld->getAngularVelocity(bodyId);
        }
        return glm::vec3(0.0f);
    }

    glm::vec3 PhysicsAdapter::getPosition(services::EntityHandle entity) const
    {
        if (!physicsWorld)
        {
            return glm::vec3(0.0f);
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            return physicsWorld->getPosition(bodyId);
        }
        return glm::vec3(0.0f);
    }

    glm::quat PhysicsAdapter::getRotation(services::EntityHandle entity) const
    {
        if (!physicsWorld)
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            return physicsWorld->getRotation(bodyId);
        }
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    void PhysicsAdapter::setPosition(services::EntityHandle entity, const glm::vec3& position)
    {
        if (!physicsWorld)
        {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            physicsWorld->setPosition(bodyId, position);
        }
    }

    void PhysicsAdapter::setRotation(services::EntityHandle entity, const glm::quat& rotation)
    {
        if (!physicsWorld)
        {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid())
        {
            physicsWorld->setRotation(bodyId, rotation);
        }
    }

    services::RaycastHit PhysicsAdapter::raycast(const glm::vec3& origin,
                                                 const glm::vec3& direction,
                                                 float maxDistance)
    {
        services::RaycastHit result;

        if (!physicsWorld)
        {
            return result;
        }

        auto physicsResult = physicsWorld->raycast(origin, direction, maxDistance);

        result.hit = physicsResult.hit;
        result.point = physicsResult.point;
        result.normal = physicsResult.normal;
        result.distance = physicsResult.distance;

        if (physicsResult.hit && physicsResult.entityId != 0)
        {
            result.entity = services::EntityHandle{physicsResult.entityId};
        }

        return result;
    }

    bool PhysicsAdapter::isOverlapping(services::EntityHandle entityA,
                                       services::EntityHandle entityB) const
    {
        if (!physicsWorld)
        {
            return false;
        }

        auto bodyA = physicsWorld->getBodyForEntity(entityA.id);
        auto bodyB = physicsWorld->getBodyForEntity(entityB.id);

        if (bodyA.IsInvalid() || bodyB.IsInvalid())
        {
            return false;
        }

        return physicsWorld->areBodiesInContact(bodyA, bodyB);
    }

    physics::RigidBodyCreateInfo PhysicsAdapter::toPhysicsBodyInfo(
        const services::RigidBodyData& data) const
    {
        physics::RigidBodyCreateInfo info;

        switch (data.type)
        {
        case services::RigidBodyData::Type::Static:
            info.type = physics::BodyType::Static;
            break;
        case services::RigidBodyData::Type::Dynamic:
            info.type = physics::BodyType::Dynamic;
            break;
        case services::RigidBodyData::Type::Kinematic:
            info.type = physics::BodyType::Kinematic;
            break;
        }

        info.mass = data.mass;
        info.linearDamping = data.linearDamping;
        info.angularDamping = data.angularDamping;
        info.linearVelocity = data.linearVelocity;
        info.angularVelocity = data.angularVelocity;

        return info;
    }

    physics::ColliderCreateInfo PhysicsAdapter::toPhysicsColliderInfo(
        const services::ColliderData& data) const
    {
        physics::ColliderCreateInfo info;

        switch (data.shape)
        {
        case services::ColliderData::Shape::Box:
            info.shape = physics::ColliderShape::Box;
            info.halfExtents = data.size * 0.5f;
            break;
        case services::ColliderData::Shape::Sphere:
            info.shape = physics::ColliderShape::Sphere;
            info.radius = data.size.x;
            break;
        case services::ColliderData::Shape::Capsule:
            info.shape = physics::ColliderShape::Capsule;
            info.radius = data.size.x;
            info.height = data.height;
            break;
        case services::ColliderData::Shape::ConvexMesh:
            info.shape = physics::ColliderShape::ConvexMesh;
            info.halfExtents = data.size * 0.5f;
            info.meshPath = data.meshPath;
            break;
        case services::ColliderData::Shape::TriangleMesh:
            info.shape = physics::ColliderShape::TriangleMesh;
            info.halfExtents = data.size * 0.5f;
            info.meshPath = data.meshPath;
            break;
        }

        info.offset = data.offset;
        info.isTrigger = data.isTrigger;
        info.collisionLayer = data.collisionLayer;

        return info;
    }

    void PhysicsAdapter::applySettings(const types::PhysicsSettings& settings)
    {
        {
            std::lock_guard<std::mutex> lock(settingsMutex);
            currentSettings = settings;
        }

        if (physicsWorld)
        {
            glm::vec3 scaledGravity = settings.gravity * settings.gravityScale;
            physicsWorld->setGravity(scaledGravity);
            physicsWorld->setCollisionMatrix(settings.collisionMatrix);
        }

        if (fixedTimestep)
        {
            fixedTimestep->setTimestep(settings.fixedTimestep);
            fixedTimestep->setMaxAccumulator(settings.maxAccumulator);
            fixedTimestep->setMaxStepsPerFrame(settings.maxStepsPerFrame);
        }
    }

    types::PhysicsSettings PhysicsAdapter::getCurrentSettings() const
    {
        std::lock_guard<std::mutex> lock(settingsMutex);
        return currentSettings;
    }

    void PhysicsAdapter::addTerrainCollider(services::EntityHandle entity,
                                             const std::vector<services::TerrainTileColliderInfo>& tiles)
    {
        if (!physicsWorld)
            return;

        for (const auto& tile : tiles)
        {
            physics::TerrainHeightFieldCreateInfo info;
            info.heightSamples = tile.heightSamples;
            info.sampleCount = tile.sampleCount;
            info.offset = glm::vec3(tile.worldOrigin.x, 0.0f, tile.worldOrigin.z);
            info.scale = glm::vec3(tile.vertexSpacing, 1.0f, tile.vertexSpacing);
            info.friction = tile.friction;
            info.restitution = tile.restitution;
            info.collisionLayer = 0; // STATIC

            physicsWorld->addTerrainTileBody(entity.id, tile.tileX, tile.tileZ, info);
        }
    }

    void PhysicsAdapter::removeTerrainCollider(services::EntityHandle entity)
    {
        if (physicsWorld)
        {
            physicsWorld->removeAllTerrainBodies(entity.id);
        }
    }

    void PhysicsAdapter::rebuildTerrainTileCollider(services::EntityHandle entity,
                                                     const services::TerrainTileColliderInfo& tile)
    {
        if (!physicsWorld)
            return;

        physicsWorld->removeTerrainTileBody(entity.id, tile.tileX, tile.tileZ);

        physics::TerrainHeightFieldCreateInfo info;
        info.heightSamples = tile.heightSamples;
        info.sampleCount = tile.sampleCount;
        info.offset = glm::vec3(tile.worldOrigin.x, 0.0f, tile.worldOrigin.z);
        info.scale = glm::vec3(tile.vertexSpacing, 1.0f, tile.vertexSpacing);
        info.friction = tile.friction;
        info.restitution = tile.restitution;
        info.collisionLayer = 0;

        physicsWorld->addTerrainTileBody(entity.id, tile.tileX, tile.tileZ, info);
    }

    bool PhysicsAdapter::hasTerrainCollider(services::EntityHandle entity) const
    {
        if (physicsWorld)
        {
            return physicsWorld->hasTerrainBodies(entity.id);
        }
        return false;
    }
}
