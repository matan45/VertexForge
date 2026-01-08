#include "PhysicsAdapter.hpp"

namespace core {

    PhysicsAdapter::PhysicsAdapter()
        : physicsWorld(std::make_unique<physics::PhysicsWorld>())
        , fixedTimestep(std::make_unique<physics::FixedTimestep>()) {
    }

    PhysicsAdapter::~PhysicsAdapter() {
        cleanUp();
    }

    bool PhysicsAdapter::init() {
        return physicsWorld->init();
    }

    void PhysicsAdapter::cleanUp() {
        if (physicsWorld) {
            physicsWorld->cleanUp();
        }
        fixedTimestep->reset();
    }

    bool PhysicsAdapter::isInitialized() const {
        return physicsWorld && physicsWorld->isInitialized();
    }

    void PhysicsAdapter::update(float deltaTime) {
        if (!isInitialized()) {
            return;
        }

        // Use fixed timestep for deterministic physics
        fixedTimestep->update(deltaTime, [this](float fixedDt) {
            physicsWorld->step(fixedDt);
        });

        // Process contact events after stepping
        physicsWorld->processContactEvents();
    }

    void PhysicsAdapter::setGravity(const glm::vec3& gravity) {
        if (physicsWorld) {
            physicsWorld->setGravity(gravity);
        }
    }

    glm::vec3 PhysicsAdapter::getGravity() const {
        if (physicsWorld) {
            return physicsWorld->getGravity();
        }
        return glm::vec3(0.0f, -9.81f, 0.0f);
    }

    void PhysicsAdapter::addRigidBody(services::EntityHandle entity,
        const services::RigidBodyData& data,
        const services::ColliderData& collider) {
        if (!physicsWorld) {
            return;
        }

        auto bodyInfo = toPhysicsBodyInfo(data);
        auto colliderInfo = toPhysicsColliderInfo(collider);

        physicsWorld->addRigidBody(entity.id, bodyInfo, colliderInfo);
    }

    void PhysicsAdapter::removeRigidBody(services::EntityHandle entity) {
        if (physicsWorld) {
            physicsWorld->removeRigidBodyByEntity(entity.id);
        }
    }

    bool PhysicsAdapter::hasRigidBody(services::EntityHandle entity) const {
        if (physicsWorld) {
            return physicsWorld->hasEntityBody(entity.id);
        }
        return false;
    }

    std::optional<services::RigidBodyData> PhysicsAdapter::getRigidBody(
        services::EntityHandle entity) const {
        if (!physicsWorld || !physicsWorld->hasEntityBody(entity.id)) {
            return std::nullopt;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (bodyId.IsInvalid()) {
            return std::nullopt;
        }

        // Return basic rigid body data
        services::RigidBodyData data;
        data.linearVelocity = physicsWorld->getLinearVelocity(bodyId);
        data.angularVelocity = physicsWorld->getAngularVelocity(bodyId);
        // Note: Other properties would need to be stored separately as Jolt doesn't
        // expose them directly through the interface
        return data;
    }

    void PhysicsAdapter::addCollider(services::EntityHandle entity,
        const services::ColliderData& data) {
        // For standalone colliders (without rigid body), create a static body
        if (!physicsWorld) {
            return;
        }

        services::RigidBodyData bodyData;
        bodyData.type = services::RigidBodyData::Type::Static;

        addRigidBody(entity, bodyData, data);
    }

    void PhysicsAdapter::removeCollider(services::EntityHandle entity) {
        // Removing collider removes the body
        removeRigidBody(entity);
    }

    void PhysicsAdapter::applyForce(services::EntityHandle entity, const glm::vec3& force) {
        if (!physicsWorld) {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            physicsWorld->applyForce(bodyId, force);
        }
    }

    void PhysicsAdapter::applyForceAtPosition(services::EntityHandle entity,
        const glm::vec3& force,
        const glm::vec3& position) {
        if (!physicsWorld) {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            physicsWorld->applyForceAtPosition(bodyId, force, position);
        }
    }

    void PhysicsAdapter::applyImpulse(services::EntityHandle entity, const glm::vec3& impulse) {
        if (!physicsWorld) {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            physicsWorld->applyImpulse(bodyId, impulse);
        }
    }

    void PhysicsAdapter::applyTorque(services::EntityHandle entity, const glm::vec3& torque) {
        if (!physicsWorld) {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            physicsWorld->applyTorque(bodyId, torque);
        }
    }

    void PhysicsAdapter::setLinearVelocity(services::EntityHandle entity,
        const glm::vec3& velocity) {
        if (!physicsWorld) {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            physicsWorld->setLinearVelocity(bodyId, velocity);
        }
    }

    glm::vec3 PhysicsAdapter::getLinearVelocity(services::EntityHandle entity) const {
        if (!physicsWorld) {
            return glm::vec3(0.0f);
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            return physicsWorld->getLinearVelocity(bodyId);
        }
        return glm::vec3(0.0f);
    }

    void PhysicsAdapter::setAngularVelocity(services::EntityHandle entity,
        const glm::vec3& velocity) {
        if (!physicsWorld) {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            physicsWorld->setAngularVelocity(bodyId, velocity);
        }
    }

    glm::vec3 PhysicsAdapter::getAngularVelocity(services::EntityHandle entity) const {
        if (!physicsWorld) {
            return glm::vec3(0.0f);
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            return physicsWorld->getAngularVelocity(bodyId);
        }
        return glm::vec3(0.0f);
    }

    glm::vec3 PhysicsAdapter::getPosition(services::EntityHandle entity) const {
        if (!physicsWorld) {
            return glm::vec3(0.0f);
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            return physicsWorld->getPosition(bodyId);
        }
        return glm::vec3(0.0f);
    }

    glm::quat PhysicsAdapter::getRotation(services::EntityHandle entity) const {
        if (!physicsWorld) {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            return physicsWorld->getRotation(bodyId);
        }
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    void PhysicsAdapter::setPosition(services::EntityHandle entity, const glm::vec3& position) {
        if (!physicsWorld) {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            physicsWorld->setPosition(bodyId, position);
        }
    }

    void PhysicsAdapter::setRotation(services::EntityHandle entity, const glm::quat& rotation) {
        if (!physicsWorld) {
            return;
        }

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (!bodyId.IsInvalid()) {
            physicsWorld->setRotation(bodyId, rotation);
        }
    }

    services::RaycastHit PhysicsAdapter::raycast(const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance) {
        services::RaycastHit result;

        if (!physicsWorld) {
            return result;
        }

        auto physicsResult = physicsWorld->raycast(origin, direction, maxDistance);

        result.hit = physicsResult.hit;
        result.point = physicsResult.point;
        result.normal = physicsResult.normal;
        result.distance = physicsResult.distance;

        if (physicsResult.hit && physicsResult.entityId != 0) {
            result.entity = services::EntityHandle{ physicsResult.entityId };
        }

        return result;
    }

    std::vector<services::RaycastHit> PhysicsAdapter::raycastAll(const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance) {
        std::vector<services::RaycastHit> results;

        if (!physicsWorld) {
            return results;
        }

        auto physicsResults = physicsWorld->raycastAll(origin, direction, maxDistance);

        for (const auto& physicsResult : physicsResults) {
            services::RaycastHit result;
            result.hit = physicsResult.hit;
            result.point = physicsResult.point;
            result.normal = physicsResult.normal;
            result.distance = physicsResult.distance;

            if (physicsResult.entityId != 0) {
                result.entity = services::EntityHandle{ physicsResult.entityId };
            }

            results.push_back(result);
        }

        return results;
    }

    bool PhysicsAdapter::isOverlapping(services::EntityHandle entityA,
        services::EntityHandle entityB) const {
        // Would require additional Jolt query - simplified for now
        // Could use ContactConstraintManager::WereBodiesInContact
        return false;
    }

    double PhysicsAdapter::getInterpolationAlpha() const {
        if (fixedTimestep) {
            return fixedTimestep->getAlpha();
        }
        return 1.0;
    }

    physics::RigidBodyCreateInfo PhysicsAdapter::toPhysicsBodyInfo(
        const services::RigidBodyData& data) const {
        physics::RigidBodyCreateInfo info;

        switch (data.type) {
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
        info.useGravity = data.useGravity;
        info.linearVelocity = data.linearVelocity;
        info.angularVelocity = data.angularVelocity;

        return info;
    }

    physics::ColliderCreateInfo PhysicsAdapter::toPhysicsColliderInfo(
        const services::ColliderData& data) const {
        physics::ColliderCreateInfo info;

        switch (data.shape) {
        case services::ColliderData::Shape::Box:
            info.shape = physics::ColliderShape::Box;
            info.halfExtents = data.size * 0.5f;  // Service uses full size, physics uses half-extents
            break;
        case services::ColliderData::Shape::Sphere:
            info.shape = physics::ColliderShape::Sphere;
            info.radius = data.size.x;  // Use x component as radius
            break;
        case services::ColliderData::Shape::Capsule:
            info.shape = physics::ColliderShape::Capsule;
            info.radius = data.size.x;
            info.height = data.height;
            break;
        case services::ColliderData::Shape::Mesh:
            // Mesh colliders will be handled separately in future
            info.shape = physics::ColliderShape::Box;
            info.halfExtents = data.size * 0.5f;
            break;
        }

        info.offset = data.offset;
        info.isTrigger = data.isTrigger;

        return info;
    }

}
