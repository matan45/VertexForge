#include "PhysicsAdapter.hpp"
#include "PhysicsConversions.hpp"
#include "../../services/events/PhysicsEvents.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../physics/PhysicsSkeletonConverter.hpp"
#include "../../graphics/animation/RuntimeAnimatorSystem.hpp"
#include "../../graphics/animation/AnimatorStateMachine.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "scene/EntityRegistry.hpp"
#include "print/EditorLogger.hpp"

namespace core
{
    namespace
    {
        template<typename Func>
        void withBody(physics::PhysicsWorld* pw, uint64_t entityId, Func&& fn)
        {
            if (!pw) return;
            auto bodyId = pw->getBodyForEntity(entityId);
            if (!bodyId.IsInvalid()) fn(bodyId);
        }

        template<typename T, typename Func>
        T queryBody(const physics::PhysicsWorld* pw, uint64_t entityId, Func&& fn, T defaultVal)
        {
            if (!pw) return defaultVal;
            auto bodyId = pw->getBodyForEntity(entityId);
            if (!bodyId.IsInvalid()) return fn(bodyId);
            return defaultVal;
        }

        struct ContactEntityPair
        {
            services::EntityHandle entityA;
            services::EntityHandle entityB;
        };

        std::optional<ContactEntityPair> resolveContactEntities(
            const physics::PhysicsWorld* pw, const physics::ContactEvent& event)
        {
            uint64_t idA = pw->getEntityForBody(event.bodyA);
            uint64_t idB = pw->getEntityForBody(event.bodyB);
            if (idA == 0 || idB == 0) return std::nullopt;
            return ContactEntityPair{{idA}, {idB}};
        }
    }

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
            return false;

        physicsWorld->setContactAddedCallback([this](const physics::ContactEvent& e) { onContactAdded(e); });
        physicsWorld->setContactRemovedCallback([this](const physics::ContactEvent& e) { onContactRemoved(e); });

        return true;
    }

    void PhysicsAdapter::onContactAdded(const physics::ContactEvent& event)
    {
        auto pair = resolveContactEntities(physicsWorld.get(), event);
        if (!pair) return;

        auto& dispatcher = events::EventDispatcher::instance();

        if (event.isSensor)
        {
            events::physics::TriggerEnterNotification notification;
            notification.triggerEntity = pair->entityA;
            notification.otherEntity = pair->entityB;
            dispatcher.publish(notification);
        }
        else
        {
            events::physics::CollisionStartNotification notification;
            notification.entityA = pair->entityA;
            notification.entityB = pair->entityB;
            notification.contactPoint = event.contactPoint;
            notification.normal = event.normal;
            notification.penetrationDepth = event.penetrationDepth;
            dispatcher.publish(notification);
        }
    }

    void PhysicsAdapter::onContactRemoved(const physics::ContactEvent& event)
    {
        auto pair = resolveContactEntities(physicsWorld.get(), event);
        if (!pair) return;

        auto& dispatcher = events::EventDispatcher::instance();

        if (event.isSensor)
        {
            events::physics::TriggerExitNotification notification;
            notification.triggerEntity = pair->entityA;
            notification.otherEntity = pair->entityB;
            dispatcher.publish(notification);
        }
        else
        {
            events::physics::CollisionEndNotification notification;
            notification.entityA = pair->entityA;
            notification.entityB = pair->entityB;
            dispatcher.publish(notification);
        }
    }

    void PhysicsAdapter::cleanUp()
    {
        physicsAnimationEntities.clear();
        if (physicsWorld)
            physicsWorld->cleanUp();
        fixedTimestep->reset();
        waterSensorEntities.clear();
    }

    bool PhysicsAdapter::isInitialized() const
    {
        return physicsWorld && physicsWorld->isInitialized();
    }

    void PhysicsAdapter::update(float deltaTime)
    {
        if (!isInitialized()) return;

        fixedTimestep->update(deltaTime, [this](float fixedDt)
        {
            physicsWorld->step(fixedDt);
        });

        physicsWorld->processContactEvents();
    }

    void PhysicsAdapter::setGravity(const glm::vec3& gravity)
    {
        if (physicsWorld) physicsWorld->setGravity(gravity);
    }

    glm::vec3 PhysicsAdapter::getGravity() const
    {
        if (physicsWorld) return physicsWorld->getGravity();
        return glm::vec3(0.0f, -9.81f, 0.0f);
    }

    void PhysicsAdapter::addRigidBody(services::EntityHandle entity,
                                      const services::RigidBodyData& data,
                                      const services::ColliderData& collider)
    {
        if (!physicsWorld) return;
        physicsWorld->addRigidBody(entity.id, toPhysicsBodyInfo(data), toPhysicsColliderInfo(collider));
    }

    void PhysicsAdapter::removeRigidBody(services::EntityHandle entity)
    {
        if (physicsWorld) physicsWorld->removeRigidBodyByEntity(entity.id);
    }

    bool PhysicsAdapter::hasRigidBody(services::EntityHandle entity) const
    {
        return physicsWorld && physicsWorld->hasEntityBody(entity.id);
    }

    std::optional<services::RigidBodyData> PhysicsAdapter::getRigidBody(
        services::EntityHandle entity) const
    {
        if (!physicsWorld || !physicsWorld->hasEntityBody(entity.id))
            return std::nullopt;

        auto bodyId = physicsWorld->getBodyForEntity(entity.id);
        if (bodyId.IsInvalid()) return std::nullopt;

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
        if (!physicsWorld) return;
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
        withBody(physicsWorld.get(), entity.id, [&](auto bodyId) {
            physicsWorld->applyForce(bodyId, force);
        });
    }

    void PhysicsAdapter::applyForceAtPosition(services::EntityHandle entity,
                                              const glm::vec3& force,
                                              const glm::vec3& position)
    {
        withBody(physicsWorld.get(), entity.id, [&](auto bodyId) {
            physicsWorld->applyForceAtPosition(bodyId, force, position);
        });
    }

    void PhysicsAdapter::applyImpulse(services::EntityHandle entity, const glm::vec3& impulse)
    {
        withBody(physicsWorld.get(), entity.id, [&](auto bodyId) {
            physicsWorld->applyImpulse(bodyId, impulse);
        });
    }

    void PhysicsAdapter::applyTorque(services::EntityHandle entity, const glm::vec3& torque)
    {
        withBody(physicsWorld.get(), entity.id, [&](auto bodyId) {
            physicsWorld->applyTorque(bodyId, torque);
        });
    }

    void PhysicsAdapter::setLinearVelocity(services::EntityHandle entity, const glm::vec3& velocity)
    {
        withBody(physicsWorld.get(), entity.id, [&](auto bodyId) {
            physicsWorld->setLinearVelocity(bodyId, velocity);
        });
    }

    glm::vec3 PhysicsAdapter::getLinearVelocity(services::EntityHandle entity) const
    {
        return queryBody(physicsWorld.get(), entity.id,
            [&](auto bodyId) { return physicsWorld->getLinearVelocity(bodyId); },
            glm::vec3(0.0f));
    }

    void PhysicsAdapter::setAngularVelocity(services::EntityHandle entity, const glm::vec3& velocity)
    {
        withBody(physicsWorld.get(), entity.id, [&](auto bodyId) {
            physicsWorld->setAngularVelocity(bodyId, velocity);
        });
    }

    glm::vec3 PhysicsAdapter::getAngularVelocity(services::EntityHandle entity) const
    {
        return queryBody(physicsWorld.get(), entity.id,
            [&](auto bodyId) { return physicsWorld->getAngularVelocity(bodyId); },
            glm::vec3(0.0f));
    }

    glm::vec3 PhysicsAdapter::getPosition(services::EntityHandle entity) const
    {
        return queryBody(physicsWorld.get(), entity.id,
            [&](auto bodyId) { return physicsWorld->getPosition(bodyId); },
            glm::vec3(0.0f));
    }

    glm::quat PhysicsAdapter::getRotation(services::EntityHandle entity) const
    {
        return queryBody(physicsWorld.get(), entity.id,
            [&](auto bodyId) { return physicsWorld->getRotation(bodyId); },
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }

    void PhysicsAdapter::setPosition(services::EntityHandle entity, const glm::vec3& position)
    {
        withBody(physicsWorld.get(), entity.id, [&](auto bodyId) {
            physicsWorld->setPosition(bodyId, position);
        });
    }

    void PhysicsAdapter::setRotation(services::EntityHandle entity, const glm::quat& rotation)
    {
        withBody(physicsWorld.get(), entity.id, [&](auto bodyId) {
            physicsWorld->setRotation(bodyId, rotation);
        });
    }

    services::RaycastHit PhysicsAdapter::raycast(const glm::vec3& origin,
                                                 const glm::vec3& direction,
                                                 float maxDistance)
    {
        services::RaycastHit result;
        if (!physicsWorld) return result;

        auto physicsResult = physicsWorld->raycast(origin, direction, maxDistance);

        result.hit = physicsResult.hit;
        result.point = physicsResult.point;
        result.normal = physicsResult.normal;
        result.distance = physicsResult.distance;

        if (physicsResult.hit && physicsResult.entityId != 0)
            result.entity = services::EntityHandle{physicsResult.entityId};

        return result;
    }

    bool PhysicsAdapter::isOverlapping(services::EntityHandle entityA,
                                       services::EntityHandle entityB) const
    {
        if (!physicsWorld) return false;

        auto bodyA = physicsWorld->getBodyForEntity(entityA.id);
        auto bodyB = physicsWorld->getBodyForEntity(entityB.id);

        if (bodyA.IsInvalid() || bodyB.IsInvalid()) return false;

        return physicsWorld->areBodiesInContact(bodyA, bodyB);
    }

    void PhysicsAdapter::applySettings(const types::PhysicsSettings& settings)
    {
        {
            std::lock_guard<std::mutex> lock(settingsMutex);
            currentSettings = settings;
        }

        if (physicsWorld)
        {
            physicsWorld->setGravity(settings.gravity * settings.gravityScale);
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
        if (!physicsWorld) return;

        for (const auto& tile : tiles)
        {
            auto info = toTerrainCreateInfo(tile);
            physicsWorld->addTerrainTileBody(entity.id, tile.tileX, tile.tileZ, info);
        }
    }

    void PhysicsAdapter::removeTerrainCollider(services::EntityHandle entity)
    {
        if (physicsWorld) physicsWorld->removeAllTerrainBodies(entity.id);
    }

    void PhysicsAdapter::rebuildTerrainTileCollider(services::EntityHandle entity,
                                                     const services::TerrainTileColliderInfo& tile)
    {
        if (!physicsWorld) return;
        physicsWorld->removeTerrainTileBody(entity.id, tile.tileX, tile.tileZ);

        auto info = toTerrainCreateInfo(tile);
        physicsWorld->addTerrainTileBody(entity.id, tile.tileX, tile.tileZ, info);
    }

    bool PhysicsAdapter::hasTerrainCollider(services::EntityHandle entity) const
    {
        return physicsWorld && physicsWorld->hasTerrainBodies(entity.id);
    }

    void PhysicsAdapter::addWaterSensorBody(services::EntityHandle entity,
                                            const glm::vec3& position,
                                            const glm::vec3& halfExtents)
    {
        if (!physicsWorld) return;

        services::ColliderData collider;
        collider.shape = services::ColliderData::Shape::Box;
        collider.size = halfExtents * 2.0f;
        collider.offset = position;
        collider.isTrigger = true;
        collider.collisionLayer = 3; // SENSOR layer

        addCollider(entity, collider);
        waterSensorEntities.insert(entity.id);
    }

    void PhysicsAdapter::removeWaterSensorBody(services::EntityHandle entity)
    {
        removeCollider(entity);
        waterSensorEntities.erase(entity.id);
    }

    bool PhysicsAdapter::hasWaterSensorBody(services::EntityHandle entity) const
    {
        return waterSensorEntities.contains(entity.id);
    }

    // ==========================================
    // Physics Animation / Ragdoll
    // ==========================================

    bool PhysicsAdapter::createPhysicsAnimation(services::EntityHandle entity,
                                                 const types::PhysicsAnimationConfig& config,
                                                 const resource::SkeletonData& skeletonData,
                                                 const glm::vec3& entityPosition,
                                                 const glm::quat& entityRotation)
    {
        if (!physicsWorld) return false;

        auto buildResult = physics::RagdollSettingsBuilder::build(config, skeletonData, entityPosition, entityRotation);
        if (!buildResult.success)
        {
            vfLogWarning("Failed to build ragdoll settings for entity {}: {}", entity.id, buildResult.errorMessage);
            return false;
        }

        if (!physicsWorld->createRagdoll(entity.id, buildResult))
        {
            vfLogWarning("Failed to create ragdoll in PhysicsWorld for entity {}", entity.id);
            return false;
        }

        PhysicsAnimationState state;
        state.buildResult = std::move(buildResult);
        state.skeletonData = skeletonData;
        physicsAnimationEntities[entity.id] = std::move(state);

        return true;
    }

    void PhysicsAdapter::destroyPhysicsAnimation(services::EntityHandle entity)
    {
        if (!physicsWorld) return;
        physicsWorld->destroyKinematicBoneBodies(entity.id);
        physicsWorld->destroyRagdoll(entity.id);
        physicsAnimationEntities.erase(entity.id);
    }

    bool PhysicsAdapter::hasPhysicsAnimation(services::EntityHandle entity) const
    {
        return physicsAnimationEntities.contains(entity.id);
    }

    void PhysicsAdapter::activateRagdoll(services::EntityHandle entity)
    {
        if (physicsWorld) physicsWorld->activateRagdoll(entity.id);
    }

    void PhysicsAdapter::deactivateRagdoll(services::EntityHandle entity)
    {
        if (physicsWorld) physicsWorld->deactivateRagdoll(entity.id);
    }

    bool PhysicsAdapter::isRagdollActive(services::EntityHandle entity) const
    {
        return physicsWorld && physicsWorld->hasRagdoll(entity.id);
    }

    bool PhysicsAdapter::createKinematicBones(services::EntityHandle entity, const glm::vec3& entityPosition)
    {
        if (!physicsWorld) return false;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return false;
        return physicsWorld->createKinematicBoneBodies(entity.id, it->second.buildResult, entityPosition);
    }

    void PhysicsAdapter::destroyKinematicBones(services::EntityHandle entity)
    {
        if (physicsWorld) physicsWorld->destroyKinematicBoneBodies(entity.id);
    }

    void PhysicsAdapter::updateKinematicBones(services::EntityHandle entity,
                                               const std::vector<glm::mat4>& boneWorldTransforms,
                                               float deltaTime)
    {
        if (!physicsWorld) return;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;

        const auto& conversion = it->second.buildResult.skeletonConversion;
        physicsWorld->updateKinematicBonePoses(entity.id, boneWorldTransforms,
                                               conversion.physicsToAnimBoneIndex, deltaTime);
    }

    std::vector<glm::mat4> PhysicsAdapter::getRagdollBoneMatrices(
        services::EntityHandle entity,
        const resource::SkeletonData& skeletonData,
        const std::vector<glm::mat4>& fallbackAnimWorldTransforms) const
    {
        if (!physicsWorld) return {};

        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return {};

        const auto& conversion = it->second.buildResult.skeletonConversion;
        JPH::SkeletonPose ragdollPose;
        ragdollPose.SetSkeleton(conversion.physicsSkeleton);

        if (!physicsWorld->getRagdollPose(entity.id, ragdollPose)) return {};

        return physics::PhysicsSkeletonConverter::ragdollPoseToSkinningMatrices(
            ragdollPose, conversion, skeletonData, fallbackAnimWorldTransforms);
    }

    void PhysicsAdapter::transitionToRagdoll(services::EntityHandle entity,
                                              const std::vector<glm::mat4>& currentBoneWorldTransforms,
                                              const glm::vec3& entityPosition)
    {
        if (!physicsWorld) return;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;

        const auto& conversion = it->second.buildResult.skeletonConversion;
        JPH::SkeletonPose currentPose = physics::PhysicsSkeletonConverter::buildPhysicsSkeletonPose(
            conversion.physicsSkeleton, currentBoneWorldTransforms,
            conversion.physicsToAnimBoneIndex, entityPosition);

        physicsWorld->transitionToRagdoll(entity.id, currentPose);
    }

    void PhysicsAdapter::transitionToKinematic(services::EntityHandle entity, const glm::vec3& entityPosition)
    {
        if (!physicsWorld) return;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;

        physicsWorld->transitionToKinematic(entity.id, it->second.buildResult, entityPosition);
    }

    void PhysicsAdapter::applyRagdollImpulse(services::EntityHandle entity, const glm::vec3& impulse)
    {
        if (physicsWorld) physicsWorld->applyRagdollImpulse(entity.id, impulse);
    }

    void PhysicsAdapter::applyRagdollBoneImpulse(services::EntityHandle entity, int animBoneIndex,
                                                  const glm::vec3& impulse)
    {
        if (!physicsWorld) return;
        auto it = physicsAnimationEntities.find(entity.id);
        if (it == physicsAnimationEntities.end()) return;

        const auto& convMap = it->second.buildResult.skeletonConversion.animToPhysicsBoneIndex;
        auto mapIt = convMap.find(animBoneIndex);
        if (mapIt == convMap.end()) return;

        physicsWorld->applyRagdollBoneImpulse(entity.id, mapIt->second, impulse);
    }

    void PhysicsAdapter::updatePhysicsAnimations(float deltaTime)
    {
        if (!physicsWorld || physicsAnimationEntities.empty()) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& animatorSystem = animation::RuntimeAnimatorSystem::instance();

        for (auto& [entityId, state] : physicsAnimationEntities)
        {
            auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
            if (!registry.valid(entity)) continue;
            if (!registry.all_of<components::PhysicsAnimationComponent>(entity)) continue;

            auto& physAnimComp = registry.get<components::PhysicsAnimationComponent>(entity);
            if (!physAnimComp.isInitialized) continue;

            if (physAnimComp.currentMode == types::PhysicsAnimationMode::Kinematic)
            {
                auto* animator = animatorSystem.getAnimator(entity);
                if (animator && animator->isInitialized())
                {
                    const auto& boneMatrices = animator->getBoneMatrices();
                    if (!boneMatrices.empty())
                    {
                        const auto& conversion = state.buildResult.skeletonConversion;
                        physicsWorld->updateKinematicBonePoses(
                            entityId, boneMatrices,
                            conversion.physicsToAnimBoneIndex, deltaTime);
                    }
                }
            }
            else if (physAnimComp.currentMode == types::PhysicsAnimationMode::Ragdoll)
            {
                const auto& conversion = state.buildResult.skeletonConversion;
                JPH::SkeletonPose ragdollPose;
                ragdollPose.SetSkeleton(conversion.physicsSkeleton);

                if (physicsWorld->getRagdollPose(entityId, ragdollPose))
                {
                    std::vector<glm::mat4> fallbackMatrices;
                    auto* animator = animatorSystem.getAnimator(entity);
                    if (animator && animator->isInitialized())
                    {
                        fallbackMatrices = animator->getBoneMatrices();
                    }

                    physAnimComp.overrideBoneMatrices =
                        physics::PhysicsSkeletonConverter::ragdollPoseToSkinningMatrices(
                            ragdollPose, conversion, state.skeletonData, fallbackMatrices);
                }
            }
        }
    }
}
