#include "PhysicsServiceImpl.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../events/physics/PhysicsSettingsEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include <algorithm>
#include <cassert>

namespace services {

    PhysicsServiceImpl::PhysicsServiceImpl(IPhysicsProvider* physicsProvider)
        : physicsProvider(physicsProvider) {
        assert(physicsProvider && "PhysicsProvider must not be null");
    }

    PhysicsServiceImpl::~PhysicsServiceImpl() = default;

    void PhysicsServiceImpl::registerEventHandlers() {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // === Commands ===

        dispatcher.registerCommandHandler<events::physics::SetGravityCommand>(
            [this](const auto& cmd) {
                setGravity(cmd.gravity);
            });

        dispatcher.registerCommandHandler<events::physics::AddRigidBodyCommand>(
            [this](const auto& cmd) {
                physicsProvider->addRigidBody(cmd.entity, cmd.rigidBody, cmd.collider);
            });

        dispatcher.registerCommandHandler<events::physics::RemoveRigidBodyCommand>(
            [this](const auto& cmd) {
                removeRigidBody(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::physics::AddColliderCommand>(
            [this](const auto& cmd) {
                addCollider(cmd.entity, cmd.collider);
            });

        dispatcher.registerCommandHandler<events::physics::RemoveColliderCommand>(
            [this](const auto& cmd) {
                removeCollider(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::physics::ApplyForceCommand>(
            [this](const auto& cmd) {
                applyForce(cmd.entity, cmd.force);
            });

        dispatcher.registerCommandHandler<events::physics::ApplyForceAtPositionCommand>(
            [this](const auto& cmd) {
                applyForceAtPosition(cmd.entity, cmd.force, cmd.position);
            });

        dispatcher.registerCommandHandler<events::physics::ApplyImpulseCommand>(
            [this](const auto& cmd) {
                applyImpulse(cmd.entity, cmd.impulse);
            });

        dispatcher.registerCommandHandler<events::physics::ApplyTorqueCommand>(
            [this](const auto& cmd) {
                applyTorque(cmd.entity, cmd.torque);
            });

        dispatcher.registerCommandHandler<events::physics::SetLinearVelocityCommand>(
            [this](const auto& cmd) {
                setLinearVelocity(cmd.entity, cmd.velocity);
            });

        dispatcher.registerCommandHandler<events::physics::SetAngularVelocityCommand>(
            [this](const auto& cmd) {
                setAngularVelocity(cmd.entity, cmd.velocity);
            });

        dispatcher.registerCommandHandler<events::physics::SetPhysicsPositionCommand>(
            [this](const auto& cmd) {
                physicsProvider->setPosition(cmd.entity, cmd.position);
            });

        dispatcher.registerCommandHandler<events::physics::SetPhysicsRotationCommand>(
            [this](const auto& cmd) {
                physicsProvider->setRotation(cmd.entity, cmd.rotation);
            });

        // === Queries ===

        dispatcher.registerQueryHandler<events::physics::GetGravityQuery>(
            [this](const auto& query) {
                return getGravity();
            });

        dispatcher.registerQueryHandler<events::physics::HasRigidBodyQuery>(
            [this](const auto& query) {
                return hasRigidBody(query.entity);
            });

        dispatcher.registerQueryHandler<events::physics::GetRigidBodyQuery>(
            [this](const auto& query) {
                return getRigidBody(query.entity);
            });

        dispatcher.registerQueryHandler<events::physics::GetLinearVelocityQuery>(
            [this](const auto& query) {
                return getLinearVelocity(query.entity);
            });

        dispatcher.registerQueryHandler<events::physics::GetAngularVelocityQuery>(
            [this](const auto& query) {
                return getAngularVelocity(query.entity);
            });

        dispatcher.registerQueryHandler<events::physics::GetPhysicsPositionQuery>(
            [this](const auto& query) {
                return physicsProvider->getPosition(query.entity);
            });

        dispatcher.registerQueryHandler<events::physics::GetPhysicsRotationQuery>(
            [this](const auto& query) {
                return physicsProvider->getRotation(query.entity);
            });

        dispatcher.registerQueryHandler<events::physics::RaycastQuery>(
            [this](const auto& query) {
                return raycast(query.origin, query.direction, query.maxDistance, query.layerMask);
            });

        dispatcher.registerQueryHandler<events::physics::RaycastAllQuery>(
            [this](const auto& query) {
                return raycastAll(query.origin, query.direction, query.maxDistance, query.layerMask);
            });

        dispatcher.registerQueryHandler<events::physics::IsOverlappingQuery>(
            [this](const auto& query) {
                return isOverlapping(query.entityA, query.entityB);
            });

        // === Script-oriented queries (individual property access) ===

        dispatcher.registerQueryHandler<events::physics::GetMassQuery>(
            [this](const auto& query) -> float {
                auto rb = getRigidBody(query.entity);
                return rb ? rb->mass : 1.0f;
            });

        dispatcher.registerQueryHandler<events::physics::GetLinearDampingQuery>(
            [this](const auto& query) -> float {
                auto rb = getRigidBody(query.entity);
                return rb ? rb->linearDamping : 0.0f;
            });

        dispatcher.registerQueryHandler<events::physics::GetAngularDampingQuery>(
            [this](const auto& query) -> float {
                auto rb = getRigidBody(query.entity);
                return rb ? rb->angularDamping : 0.05f;
            });

        dispatcher.registerQueryHandler<events::physics::GetBodyTypeQuery>(
            [this](const auto& query) -> RigidBodyData::Type {
                auto rb = getRigidBody(query.entity);
                return rb ? rb->type : RigidBodyData::Type::Dynamic;
            });

        // === Physics Settings Commands ===

        dispatcher.registerCommandHandler<events::physics::ApplyPhysicsSettingsCommand>(
            [this](const auto& cmd) {
                physicsProvider->applySettings(cmd.settings);
                return true;
            });

        dispatcher.registerCommandHandler<events::physics::SetLayerCollisionCommand>(
            [this](const auto& cmd) {
                auto settings = physicsProvider->getCurrentSettings();
                settings.setLayerCollision(cmd.layer1, cmd.layer2, cmd.shouldCollide);
                physicsProvider->applySettings(settings);
            });

        dispatcher.registerCommandHandler<events::physics::AddCollisionLayerCommand>(
            [this](const auto& cmd) {
                auto settings = physicsProvider->getCurrentSettings();

                // Check if we've reached the maximum number of layers
                uint8_t nextIndex = settings.getNextAvailableLayerIndex();
                if (nextIndex >= types::PhysicsSettings::MAX_LAYERS) {
                    return false;  // Cannot add more layers
                }

                // Check for empty name
                if (cmd.name.empty()) {
                    return false;
                }

                // Add the new layer
                types::CollisionLayer newLayer;
                newLayer.name = cmd.name;
                newLayer.index = nextIndex;
                newLayer.isBuiltIn = false;
                settings.layers.push_back(newLayer);

                physicsProvider->applySettings(settings);
                return true;
            });

        dispatcher.registerCommandHandler<events::physics::RemoveCollisionLayerCommand>(
            [this](const auto& cmd) {
                auto settings = physicsProvider->getCurrentSettings();

                // Find the layer by index
                auto it = std::find_if(settings.layers.begin(), settings.layers.end(),
                    [&cmd](const types::CollisionLayer& layer) {
                        return layer.index == cmd.layerIndex;
                    });

                if (it == settings.layers.end()) {
                    return false;  // Layer not found
                }

                // Cannot remove built-in layers
                if (it->isBuiltIn) {
                    return false;
                }

                settings.layers.erase(it);
                physicsProvider->applySettings(settings);
                return true;
            });

        dispatcher.registerCommandHandler<events::physics::RenameCollisionLayerCommand>(
            [this](const auto& cmd) {
                auto settings = physicsProvider->getCurrentSettings();

                // Find the layer by index
                auto it = std::find_if(settings.layers.begin(), settings.layers.end(),
                    [&cmd](const types::CollisionLayer& layer) {
                        return layer.index == cmd.layerIndex;
                    });

                if (it == settings.layers.end()) {
                    return false;  // Layer not found
                }

                // Cannot rename built-in layers
                if (it->isBuiltIn) {
                    return false;
                }

                // Check for empty name
                if (cmd.newName.empty()) {
                    return false;
                }

                it->name = cmd.newName;
                physicsProvider->applySettings(settings);
                return true;
            });

        // === Physics Settings Queries ===

        dispatcher.registerQueryHandler<events::physics::GetPhysicsSettingsQuery>(
            [this](const auto& query) {
                return physicsProvider->getCurrentSettings();
            });

        dispatcher.registerQueryHandler<events::physics::GetCollisionLayersQuery>(
            [this](const auto& query) {
                return physicsProvider->getCurrentSettings().layers;
            });

        dispatcher.registerQueryHandler<events::physics::GetLayerNameQuery>(
            [this](const auto& query) {
                const auto& settings = physicsProvider->getCurrentSettings();
                const auto* layer = settings.getLayerByIndex(query.layerIndex);
                return layer ? layer->name : std::string("Unknown");
            });

        dispatcher.registerQueryHandler<events::physics::ShouldLayersCollideQuery>(
            [this](const auto& query) {
                return physicsProvider->getCurrentSettings().shouldLayersCollide(query.layer1, query.layer2);
            });
    }

    void PhysicsServiceImpl::stepSimulation(float deltaTime) {
        physicsProvider->update(deltaTime);
    }

    void PhysicsServiceImpl::setGravity(const glm::vec3& gravity) {
        physicsProvider->setGravity(gravity);
    }

    glm::vec3 PhysicsServiceImpl::getGravity() const {
        return physicsProvider->getGravity();
    }

    void PhysicsServiceImpl::addRigidBody(EntityHandle entity, const RigidBodyData& data) {
        // Create a default box collider if not specified
        ColliderData defaultCollider;
        defaultCollider.shape = ColliderData::Shape::Box;
        defaultCollider.size = glm::vec3(1.0f);
        physicsProvider->addRigidBody(entity, data, defaultCollider);
    }

    void PhysicsServiceImpl::removeRigidBody(EntityHandle entity) {
        physicsProvider->removeRigidBody(entity);
    }

    bool PhysicsServiceImpl::hasRigidBody(EntityHandle entity) const {
        return physicsProvider->hasRigidBody(entity);
    }

    std::optional<RigidBodyData> PhysicsServiceImpl::getRigidBody(EntityHandle entity) const {
        return physicsProvider->getRigidBody(entity);
    }

    void PhysicsServiceImpl::addCollider(EntityHandle entity, const ColliderData& data) {
        physicsProvider->addCollider(entity, data);
    }

    void PhysicsServiceImpl::removeCollider(EntityHandle entity) {
        physicsProvider->removeCollider(entity);
    }

    void PhysicsServiceImpl::applyForce(EntityHandle entity, const glm::vec3& force) {
        physicsProvider->applyForce(entity, force);
    }

    void PhysicsServiceImpl::applyForceAtPosition(EntityHandle entity, const glm::vec3& force,
        const glm::vec3& position) {
        physicsProvider->applyForceAtPosition(entity, force, position);
    }

    void PhysicsServiceImpl::applyImpulse(EntityHandle entity, const glm::vec3& impulse) {
        physicsProvider->applyImpulse(entity, impulse);
    }

    void PhysicsServiceImpl::applyTorque(EntityHandle entity, const glm::vec3& torque) {
        physicsProvider->applyTorque(entity, torque);
    }

    void PhysicsServiceImpl::setLinearVelocity(EntityHandle entity, const glm::vec3& velocity) {
        physicsProvider->setLinearVelocity(entity, velocity);
    }

    glm::vec3 PhysicsServiceImpl::getLinearVelocity(EntityHandle entity) const {
        return physicsProvider->getLinearVelocity(entity);
    }

    void PhysicsServiceImpl::setAngularVelocity(EntityHandle entity, const glm::vec3& velocity) {
        physicsProvider->setAngularVelocity(entity, velocity);
    }

    glm::vec3 PhysicsServiceImpl::getAngularVelocity(EntityHandle entity) const {
        return physicsProvider->getAngularVelocity(entity);
    }

    RaycastHit PhysicsServiceImpl::raycast(const glm::vec3& origin, const glm::vec3& direction,
        float maxDistance, uint16_t layerMask) {
        return physicsProvider->raycast(origin, direction, maxDistance, layerMask);
    }

    std::vector<RaycastHit> PhysicsServiceImpl::raycastAll(const glm::vec3& origin, const glm::vec3& direction,
        float maxDistance, uint16_t layerMask) {
        return physicsProvider->raycastAll(origin, direction, maxDistance, layerMask);
    }

    bool PhysicsServiceImpl::isOverlapping(EntityHandle entityA, EntityHandle entityB) const {
        return physicsProvider->isOverlapping(entityA, entityB);
    }

}
