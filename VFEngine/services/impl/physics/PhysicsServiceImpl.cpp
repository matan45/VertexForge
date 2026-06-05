#include "PhysicsServiceImpl.hpp"
#include "PhysicsBodyBuilder.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../events/physics/PhysicsSettingsEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cassert>

namespace services {

    PhysicsServiceImpl::PhysicsServiceImpl(IPhysicsProvider* physicsProvider)
        : physicsProvider(physicsProvider) {
        assert(physicsProvider && "PhysicsProvider must not be null");
    }

    PhysicsServiceImpl::~PhysicsServiceImpl() {
        ::events::EventDispatcher::instance().unsubscribe(entityDeletedToken);
    }

    void PhysicsServiceImpl::registerEventHandlers() {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // === Commands ===

        dispatcher.registerCommandHandler<events::physics::SetGravityCommand>(
            [this](const auto& cmd) {
                setGravity(cmd.gravity);
            });

        dispatcher.registerCommandHandler<events::physics::AddRigidBodyCommand>(
            [this](const auto& cmd) {
                try {
                    physicsProvider->addRigidBody(cmd.entity, cmd.rigidBody, cmd.collider);
                    events::physics::RigidBodyAddedNotification notification;
                    notification.entity = cmd.entity;
                    ::events::EventDispatcher::instance().publish(notification);
                } catch (const std::exception& e) {
                    vfLogError("Failed to add rigid body for entity {}: {}", cmd.entity.id, e.what());
                }
            });

        dispatcher.registerCommandHandler<events::physics::RemoveRigidBodyCommand>(
            [this](const auto& cmd) {
                removeRigidBody(cmd.entity);
                events::physics::RigidBodyRemovedNotification notification;
                notification.entity = cmd.entity;
                ::events::EventDispatcher::instance().publish(notification);
            });

        dispatcher.registerCommandHandler<events::physics::AddColliderCommand>(
            [this](const auto& cmd) {
                addCollider(cmd.entity, cmd.collider);
            });

        dispatcher.registerCommandHandler<events::physics::RemoveColliderCommand>(
            [this](const auto& cmd) {
                removeCollider(cmd.entity);
            });

        // VK-1351: build a real physics body from an entity's runtime-configured
        // Collider/RigidBody components, positioned at its current transform.
        dispatcher.registerCommandHandler<events::physics::CreatePhysicsBodyCommand>(
            [this](const events::physics::CreatePhysicsBodyCommand& cmd) -> bool {
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!internal::isValidHandle(cmd.entity, registry)) return false;
                entt::entity entity = internal::fromHandle(cmd.entity);
                if (!registry.all_of<components::TransformComponent>(entity)) return false;

                const bool hasCollider = registry.all_of<components::ColliderComponent>(entity);
                const bool hasBody = registry.all_of<components::RigidBodyComponent>(entity);
                if (!hasCollider && !hasBody) return false;

                if (physicsProvider->hasRigidBody(cmd.entity))
                {
                    if (!cmd.rebuild) return false;   // idempotent: createBody is a no-op if a body exists
                    physicsProvider->removeRigidBody(cmd.entity);
                }

                const auto& transform = registry.get<components::TransformComponent>(entity);

                RigidBodyData rbData;
                if (hasBody)
                    rbData = buildRigidBodyData(registry.get<components::RigidBodyComponent>(entity));
                else
                    rbData.type = RigidBodyData::Type::Static;

                ColliderData colData;
                if (hasCollider)
                {
                    const auto& collider = registry.get<components::ColliderComponent>(entity);
                    if (hasBody)
                    {
                        const std::string entityName = registry.all_of<components::NameComponent>(entity)
                            ? registry.get<components::NameComponent>(entity).name : std::string("Runtime");
                        const std::string validationError = validateCollider(
                            collider, registry.get<components::RigidBodyComponent>(entity), entityName);
                        if (!validationError.empty())
                        {
                            vfLogWarning("{} - skipping runtime physics body creation", validationError);
                            return false;
                        }
                    }
                    colData = buildColliderData(collider, entity, registry);
                }
                else
                {
                    colData.shape = ColliderData::Shape::Box;
                    colData.size = glm::vec3(1.0f);
                }
                applyScaleToCollider(colData, transform.scale);

                try {
                    physicsProvider->addRigidBody(cmd.entity, rbData, colData);
                    physicsProvider->setPosition(cmd.entity, transform.position);
                    physicsProvider->setRotation(cmd.entity, glm::quat(glm::radians(transform.rotation)));
                } catch (const std::exception& e) {
                    vfLogError("Failed to create runtime physics body for entity {}: {}", cmd.entity.id, e.what());
                    return false;
                }

                events::physics::RigidBodyAddedNotification notification;
                notification.entity = cmd.entity;
                ::events::EventDispatcher::instance().publish(notification);
                return true;
            });

        dispatcher.registerCommandHandler<events::physics::DestroyPhysicsBodyCommand>(
            [this](const events::physics::DestroyPhysicsBodyCommand& cmd) -> bool {
                if (!physicsProvider->hasRigidBody(cmd.entity)) return false;
                removeRigidBody(cmd.entity);
                events::physics::RigidBodyRemovedNotification notification;
                notification.entity = cmd.entity;
                ::events::EventDispatcher::instance().publish(notification);
                return true;
            });

        // Static Jolt height-field body from raw samples (plugin/runtime custom terrain).
        // Reuses the terrain-tile collider machinery, keyed by (entity, tileX, tileZ).
        dispatcher.registerCommandHandler<events::physics::CreateHeightFieldBodyCommand>(
            [this](const events::physics::CreateHeightFieldBodyCommand& cmd) -> bool {
                if (cmd.sampleCount == 0
                    || cmd.heightSamples.size() < static_cast<size_t>(cmd.sampleCount) * cmd.sampleCount)
                {
                    vfLogWarning("CreateHeightFieldBody: invalid samples ({} provided, {}x{} required)",
                                 cmd.heightSamples.size(), cmd.sampleCount, cmd.sampleCount);
                    return false;
                }

                TerrainTileColliderInfo info;
                info.tileX = cmd.tileX;
                info.tileZ = cmd.tileZ;
                info.heightSamples = cmd.heightSamples.data();
                info.sampleCount = cmd.sampleCount;
                info.worldOrigin = cmd.worldOrigin;
                info.vertexSpacing = cmd.vertexSpacing;
                info.friction = cmd.friction;
                info.restitution = cmd.restitution;
                info.collisionLayer = cmd.collisionLayer;
                physicsProvider->addTerrainTileCollider(cmd.entity, info);
                return true;
            });

        dispatcher.registerCommandHandler<events::physics::DestroyHeightFieldBodyCommand>(
            [this](const events::physics::DestroyHeightFieldBodyCommand& cmd) {
                physicsProvider->removeTerrainTileCollider(cmd.entity, cmd.tileX, cmd.tileZ);
            });

        // VK-1351: tear down the physics body when its entity is destroyed, so runtime-spawned
        // (and load-time) bodies don't leak in the physics world / PhysicsBodyRegistry.
        entityDeletedToken = dispatcher.subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& notif) {
                if (physicsProvider->hasRigidBody(notif.entity))
                {
                    removeRigidBody(notif.entity);
                    events::physics::RigidBodyRemovedNotification notification;
                    notification.entity = notif.entity;
                    ::events::EventDispatcher::instance().publish(notification);
                }
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

        dispatcher.registerQueryHandler<events::physics::IsBodySleepingQuery>(
            [this](const auto& query) -> bool {
                if (!physicsProvider) return false;
                return physicsProvider->isBodySleeping(query.entity);
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
