#include "PhysicsAdapter.hpp"
#include "PhysicsConversions.hpp"
#include "threading/JobSystem.hpp"
#include "../../physics/PhysicsShapeFactory.hpp"
#include "../../physics/FixedTimestepMath.hpp"
#include "../../services/events/physics/PhysicsEvents.hpp"
#include "../../services/events/lifecycle/AssetLifecycleEvents.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "resource/AssetTypes.hpp"
#include "print/Log.hpp"

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
        auto& dispatcher = events::EventDispatcher::instance();
        assetReleaseToken = dispatcher.subscribe<events::lifecycle::AssetReleaseReadyNotification>(
            [](const events::lifecycle::AssetReleaseReadyNotification& notification)
            {
                if (notification.type == resource::AssetType::PhysicsShape ||
                    notification.type == resource::AssetType::Mesh)
                {
                    physics::PhysicsShapeFactory::evictFromCache(notification.path);
                    vfLogInfo("PhysicsAdapter: Evicted cached physics shapes for '{}'", notification.path);
                }
            });
    }

    PhysicsAdapter::~PhysicsAdapter()
    {
        if (assetReleaseToken.isValid())
        {
            events::EventDispatcher::instance().unsubscribe(assetReleaseToken);
        }
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
        // Await any in-flight async physics step before cleanup
        if (asyncStepInFlight && asyncStepFuture.valid())
        {
            asyncStepFuture.get();
            asyncStepInFlight = false;
        }

        physicsAnimationEntities.clear();
        if (physicsWorld)
            physicsWorld->cleanUp();
        fixedTimestep->reset();
        waterSensorEntities.clear();
        physics::PhysicsShapeFactory::clearCache();
    }

    bool PhysicsAdapter::isInitialized() const
    {
        return physicsWorld && physicsWorld->isInitialized();
    }

    void PhysicsAdapter::update(float deltaTime)
    {
        // Synchronous compatibility shim: kicks and immediately awaits.
        // For async overlap, use kickPhysicsStep/syncPhysicsStep separately
        // via the frame task graph (PhysicsKick / PhysicsSync tasks).
        kickPhysicsStep(deltaTime);
        syncPhysicsStep();
    }

    void PhysicsAdapter::kickPhysicsStep(float deltaTime)
    {
        if (!isInitialized()) return;

        physicsWorld->applyPendingVehicleInputs();

        asyncStepFuture = threading::JobSystem::instance().submit(
            [this, deltaTime]() -> physics::FixedTimestepResult
            {
                auto result = fixedTimestep->update(deltaTime, [this](float fixedDt)
                {
                    physicsWorld->step(fixedDt);
                });

                // Advance the interpolation double-buffer ONLY on a frame that actually stepped.
                // swap makes the previous frame's write buffer the new read (prev) buffer, then
                // capture writes the post-step state into the new write (curr) buffer, so the two
                // buffers bracket [prev step, curr step] (both writes done here on the worker thread,
                // the Jolt sim is finished, and the sole reader — getInterpolatedTransform — only runs
                // after syncPhysicsStep joins this job). On a zero-step frame we leave both buffers
                // untouched so getInterpolatedTransform keeps interpolating from prev toward curr as
                // alpha grows; swapping+capturing there would collapse read==curr==write and pin the
                // render to curr, making bodies judder at the physics rate above the display rate.
                if (result.stepsTaken > 0)
                {
                    physicsWorld->swapStateBuffers();
                    physicsWorld->captureState();
                }
                return result;
            }, threading::JobPriority::HIGH);

        asyncStepInFlight = true;
    }

    void PhysicsAdapter::syncPhysicsStep()
    {
        if (!asyncStepInFlight) return;

        auto result = asyncStepFuture.get();
        asyncStepInFlight = false;
        lastStepAlpha = static_cast<float>(result.alpha);
        lastStepCount = result.stepsTaken;

        // Process contact events on main thread
        physicsWorld->processContactEvents();

        // Run script fixedUpdate N times (once per sub-step) for determinism
        if (postStepCallback && result.stepsTaken > 0)
        {
            float fixedDt = static_cast<float>(fixedTimestep->getTimestep());
            for (int i = 0; i < result.stepsTaken; ++i)
            {
                postStepCallback(fixedDt);
            }
        }
    }

    float PhysicsAdapter::getInterpolationAlpha() const
    {
        return lastStepAlpha;
    }

    int PhysicsAdapter::getPhysicsStepsTaken() const
    {
        return lastStepCount;
    }

    float PhysicsAdapter::getFixedTimestep() const
    {
        return fixedTimestep ? static_cast<float>(fixedTimestep->getTimestep())
                             : static_cast<float>(1.0 / 60.0);
    }

    services::PhysicsTransformSnapshot PhysicsAdapter::getInterpolatedTransform(
        services::EntityHandle entity) const
    {
        services::PhysicsTransformSnapshot result;
        uint64_t entityId = entity.id;

        const auto& stateBuffer = physicsWorld->getStateBuffer();
        const auto* curr = stateBuffer.findInWriteBuffer(entityId);
        const auto* prev = stateBuffer.findInReadBuffer(entityId);

        if (curr && prev)
        {
            // interp* clamp alpha to [0,1] so a hitch (where alpha could momentarily
            // exceed 1 before the drop-remainder guard) never extrapolates the pose.
            float alpha = lastStepAlpha;
            result.position = physics::interpVec3(prev->position, curr->position, alpha);
            result.rotation = physics::interpRotation(prev->rotation, curr->rotation, alpha);
            result.linearVelocity = physics::interpVec3(prev->linearVelocity, curr->linearVelocity, alpha);
        }
        else if (curr)
        {
            result.position = curr->position;
            result.rotation = curr->rotation;
            result.linearVelocity = curr->linearVelocity;
        }
        else
        {
            // Fallback to live Jolt state
            result.position = getPosition(entity);
            result.rotation = getRotation(entity);
        }

        return result;
    }

    void PhysicsAdapter::setPostStepCallback(std::function<void(float)> callback)
    {
        postStepCallback = std::move(callback);
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

    bool PhysicsAdapter::isBodySleeping(services::EntityHandle entity) const
    {
        return queryBody(physicsWorld.get(), entity.id,
            [&](auto bodyId) { return !physicsWorld->isBodyActive(bodyId); },
            false);
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
                                                 float maxDistance, uint16_t layerMask)
    {
        services::RaycastHit result;
        if (!physicsWorld) return result;

        auto physicsResult = physicsWorld->raycast(origin, direction, maxDistance, layerMask);

        result.hit = physicsResult.hit;
        result.point = physicsResult.point;
        result.normal = physicsResult.normal;
        result.distance = physicsResult.distance;

        if (physicsResult.hit && physicsResult.entityId != 0)
            result.entity = services::EntityHandle{physicsResult.entityId};

        return result;
    }

    std::vector<services::RaycastHit> PhysicsAdapter::raycastAll(const glm::vec3& origin,
                                                                const glm::vec3& direction,
                                                                float maxDistance, uint16_t layerMask)
    {
        std::vector<services::RaycastHit> results;
        if (!physicsWorld) return results;

        auto physicsResults = physicsWorld->raycastAll(origin, direction, maxDistance, layerMask);
        results.reserve(physicsResults.size());

        for (const auto& pr : physicsResults)
        {
            services::RaycastHit hit;
            hit.hit = pr.hit;
            hit.point = pr.point;
            hit.normal = pr.normal;
            hit.distance = pr.distance;
            if (pr.entityId != 0)
                hit.entity = services::EntityHandle{pr.entityId};
            results.push_back(hit);
        }

        return results;
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

    std::vector<services::EntityHandle> PhysicsAdapter::overlapSphere(const glm::vec3& center,
                                                                      float radius,
                                                                      uint16_t layerMask) const
    {
        std::vector<services::EntityHandle> results;
        if (!physicsWorld) return results;

        auto ids = physicsWorld->overlapSphere(center, radius, layerMask);
        results.reserve(ids.size());
        for (uint64_t id : ids)
        {
            if (id != 0) results.push_back(services::EntityHandle{id});
        }
        return results;
    }

    std::vector<services::EntityHandle> PhysicsAdapter::overlapBox(const glm::vec3& center,
                                                                   const glm::vec3& halfExtents,
                                                                   const glm::quat& rotation,
                                                                   uint16_t layerMask) const
    {
        std::vector<services::EntityHandle> results;
        if (!physicsWorld) return results;

        auto ids = physicsWorld->overlapBox(center, halfExtents, rotation, layerMask);
        results.reserve(ids.size());
        for (uint64_t id : ids)
        {
            if (id != 0) results.push_back(services::EntityHandle{id});
        }
        return results;
    }

    std::vector<services::EntityHandle> PhysicsAdapter::overlapCapsule(const glm::vec3& center,
                                                                       float halfHeight, float radius,
                                                                       const glm::quat& rotation,
                                                                       uint16_t layerMask) const
    {
        std::vector<services::EntityHandle> results;
        if (!physicsWorld) return results;

        auto ids = physicsWorld->overlapCapsule(center, halfHeight, radius, rotation, layerMask);
        results.reserve(ids.size());
        for (uint64_t id : ids)
        {
            if (id != 0) results.push_back(services::EntityHandle{id});
        }
        return results;
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

    void PhysicsAdapter::addTerrainTileCollider(services::EntityHandle entity,
                                                 const services::TerrainTileColliderInfo& tile)
    {
        if (!physicsWorld) return;
        auto info = toTerrainCreateInfo(tile);
        physicsWorld->addTerrainTileBody(entity.id, tile.tileX, tile.tileZ, info);
    }

    void PhysicsAdapter::removeTerrainTileCollider(services::EntityHandle entity,
                                                    int32_t tileX, int32_t tileZ)
    {
        if (!physicsWorld) return;
        physicsWorld->getTerrainManager().cancelPendingCollider(tileX, tileZ);
        physicsWorld->removeTerrainTileBody(entity.id, tileX, tileZ);
    }

    void PhysicsAdapter::submitAsyncTerrainTileCollider(services::EntityHandle entity,
                                                          const services::TerrainTileColliderInfo& tile,
                                                          float distanceToCamera)
    {
        if (!physicsWorld) return;

        physics::OwnedTerrainColliderData data;
        data.entityId = entity.id;
        data.tileX = tile.tileX;
        data.tileZ = tile.tileZ;
        data.heightSamples.assign(tile.heightSamples,
                                   tile.heightSamples + static_cast<size_t>(tile.sampleCount) * tile.sampleCount);
        data.sampleCount = tile.sampleCount;
        data.worldOrigin = tile.worldOrigin;
        data.vertexSpacing = tile.vertexSpacing;
        data.friction = tile.friction;
        data.restitution = tile.restitution;
        data.collisionLayer = tile.collisionLayer;
        data.physicsLOD = physicsWorld->getTerrainManager().selectPhysicsLOD(distanceToCamera);

        physicsWorld->getTerrainManager().submitAsyncCollider(std::move(data));
    }

    void PhysicsAdapter::updatePhysicsColliderStreaming(const glm::vec3& cameraPosition)
    {
        if (!physicsWorld) return;
        physicsWorld->getTerrainManager().updateColliderStreaming(cameraPosition);
    }

    void PhysicsAdapter::setPhysicsColliderStreamConfig(float memoryBudgetMB, int maxCreationsPerFrame,
                                                          float lodDist0, float lodDist1, float lodDist2)
    {
        if (!physicsWorld) return;
        physics::PhysicsColliderStreamConfig config;
        config.memoryBudgetBytes = static_cast<size_t>(memoryBudgetMB * 1024.0f * 1024.0f);
        config.maxCreationsPerFrame = static_cast<uint32_t>(maxCreationsPerFrame);
        config.lodDistances[0] = lodDist0;
        config.lodDistances[1] = lodDist1;
        config.lodDistances[2] = lodDist2;
        physicsWorld->getTerrainManager().setStreamConfig(config);
    }

    PhysicsAdapter::PhysicsColliderStreamConfigDTO PhysicsAdapter::getPhysicsColliderStreamConfig() const
    {
        PhysicsColliderStreamConfigDTO dto;
        if (!physicsWorld) return dto;
        const auto& cfg = physicsWorld->getTerrainManager().getStreamConfig();
        dto.memoryBudgetMB = static_cast<float>(cfg.memoryBudgetBytes) / (1024.0f * 1024.0f);
        dto.maxCreationsPerFrame = static_cast<int>(cfg.maxCreationsPerFrame);
        dto.lodDistance0 = cfg.lodDistances[0];
        dto.lodDistance1 = cfg.lodDistances[1];
        dto.lodDistance2 = cfg.lodDistances[2];
        return dto;
    }

    void PhysicsAdapter::addCaveTileCollider(services::EntityHandle entity,
                                              const services::CaveTileColliderInfo& cave)
    {
        if (!physicsWorld) return;
        physicsWorld->addCaveTileBody(entity.id, cave.tileX, cave.tileZ, cave);
    }

    void PhysicsAdapter::removeCaveTileCollider(services::EntityHandle entity,
                                                 int32_t tileX, int32_t tileZ)
    {
        if (!physicsWorld) return;
        physicsWorld->removeCaveTileBody(entity.id, tileX, tileZ);
    }

    void PhysicsAdapter::rebuildCaveTileCollider(services::EntityHandle entity,
                                                  const services::CaveTileColliderInfo& cave)
    {
        if (!physicsWorld) return;
        physicsWorld->removeCaveTileBody(entity.id, cave.tileX, cave.tileZ);
        physicsWorld->addCaveTileBody(entity.id, cave.tileX, cave.tileZ, cave);
    }

    void PhysicsAdapter::addVegetationTileColliders(int32_t tileX, int32_t tileZ,
                                                     const std::vector<VegetationColliderInstance>& instances)
    {
        if (!physicsWorld || instances.empty()) return;

        std::vector<JPH::BodyID> bodyIds;
        bodyIds.reserve(instances.size());

        for (const auto& inst : instances)
        {
            if (inst.scale <= 0.0f || inst.height * inst.scale < 0.01f)
                continue;

            JPH::BodyID bodyId = physicsWorld->addStaticCapsule(
                inst.position, inst.rotation, inst.scale,
                inst.radius, inst.height, 0); // layer 0 = STATIC

            if (!bodyId.IsInvalid())
            {
                bodyIds.push_back(bodyId);
            }
            else
            {
                vfLogWarning("PhysicsAdapter: Failed to create vegetation collider at ({}, {}, {}) scale={} for tile ({}, {})",
                    inst.position.x, inst.position.y, inst.position.z, inst.scale, tileX, tileZ);
            }
        }

        if (!bodyIds.empty())
        {
            physicsWorld->addVegetationTileColliders(tileX, tileZ, bodyIds);
        }
    }

    void PhysicsAdapter::removeVegetationTileColliders(int32_t tileX, int32_t tileZ)
    {
        if (!physicsWorld) return;
        physicsWorld->removeVegetationTileColliders(tileX, tileZ);
    }

    void PhysicsAdapter::removeAllVegetationColliders()
    {
        if (!physicsWorld) return;
        physicsWorld->removeAllVegetationColliders();
    }

    uint32_t PhysicsAdapter::createFoliageStaticBody(const FoliageColliderDesc& desc)
    {
        if (!physicsWorld) return INVALID_BODY_HANDLE;

        physics::FoliageColliderCreateInfo info;
        info.shape = desc.shape;
        info.meshPath = desc.meshPath;
        info.position = desc.position;
        info.yRotation = desc.rotationY;
        info.scale = desc.scale;
        info.localAabbCenter = desc.localAabbCenter;
        info.localAabbHalfExtents = desc.localAabbHalfExtents;
        info.collisionLayer = desc.collisionLayer;

        JPH::BodyID id = physicsWorld->createFoliageStaticBody(info);
        // GetIndexAndSequenceNumber() already yields cInvalidBodyID (0xFFFFFFFF == INVALID_BODY_HANDLE)
        // when the body wasn't created, so no special-casing is needed here.
        return id.GetIndexAndSequenceNumber();
    }

    void PhysicsAdapter::destroyFoliageStaticBody(uint32_t bodyHandle)
    {
        if (!physicsWorld || bodyHandle == INVALID_BODY_HANDLE) return;
        physicsWorld->destroyStaticBody(JPH::BodyID(bodyHandle));
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

    bool PhysicsAdapter::addCharacterController(services::EntityHandle entity,
                                                 const CharacterControllerInfo& info,
                                                 const glm::vec3& position,
                                                 const glm::quat& rotation)
    {
        if (!physicsWorld) return false;

        physics::CharacterCreateInfo createInfo;
        createInfo.shape = info.shape;
        createInfo.size = info.size;
        createInfo.maxSlopeAngle = info.maxSlopeAngle;
        createInfo.stepHeight = info.stepHeight;
        createInfo.collisionLayer = info.collisionLayer;
        createInfo.position = position;
        createInfo.rotation = rotation;

        return physicsWorld->addCharacter(entity.id, createInfo);
    }

    void PhysicsAdapter::removeCharacterController(services::EntityHandle entity)
    {
        if (physicsWorld) physicsWorld->removeCharacter(entity.id);
    }

    bool PhysicsAdapter::hasCharacterController(services::EntityHandle entity) const
    {
        if (!physicsWorld) return false;
        return physicsWorld->hasCharacter(entity.id);
    }

    PhysicsAdapter::CharacterUpdateResult PhysicsAdapter::updateCharacterController(
        services::EntityHandle entity,
        const glm::vec3& desiredVelocity,
        float deltaTime)
    {
        CharacterUpdateResult result;
        if (!physicsWorld) return result;

        auto gravity = physicsWorld->getGravity();
        auto physResult = physicsWorld->updateCharacter(entity.id, desiredVelocity, deltaTime, gravity);

        result.position = physResult.position;
        result.linearVelocity = physResult.linearVelocity;
        result.isGrounded = physResult.isGrounded;
        result.groundNormal = physResult.groundNormal;
        result.groundVelocity = physResult.groundVelocity;
        return result;
    }

    bool PhysicsAdapter::isCharacterGrounded(services::EntityHandle entity) const
    {
        if (!physicsWorld) return false;
        return physicsWorld->isCharacterGrounded(entity.id);
    }

    glm::vec3 PhysicsAdapter::getCharacterPosition(services::EntityHandle entity) const
    {
        if (!physicsWorld) return glm::vec3(0.0f);
        return physicsWorld->getCharacterPosition(entity.id);
    }

    glm::vec3 PhysicsAdapter::getCharacterVelocity(services::EntityHandle entity) const
    {
        if (!physicsWorld) return glm::vec3(0.0f);
        return physicsWorld->getCharacterLinearVelocity(entity.id);
    }

    void PhysicsAdapter::setCharacterPosition(services::EntityHandle entity, const glm::vec3& position)
    {
        if (physicsWorld) physicsWorld->setCharacterPosition(entity.id, position);
    }
}
