#include "PhysicsTerrainManager.hpp"
#include "PhysicsHeightFieldDecimator.hpp"
#include "PhysicsContext.hpp"
#include "PhysicsShapeFactory.hpp"
#include "JoltConversions.hpp"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <glm/gtc/quaternion.hpp>
#include "print/Log.hpp"
#include <algorithm>
#include <chrono>

namespace core::physics
{
    void PhysicsTerrainManager::init(PhysicsContext* context)
    {
        ctx = context;
    }

    void PhysicsTerrainManager::cleanUp()
    {
        // Wait for all pending async shape builds to complete
        for (auto& pending : pendingColliders)
        {
            if (pending.shapeFuture.valid())
                pending.shapeFuture.wait();
        }
        pendingColliders.clear();
        pendingColliderKeys.clear();
        colliderStreamInfos.clear();
        currentPhysicsMemory = 0;
        pendingMemoryReserved = 0;

        if (!ctx || !ctx->physicsSystem) return;

        auto& bodyInterface = ctx->getBodyInterface();

        for (auto& [entityId, tileMap] : terrainBodies)
            for (auto& [tileKey, bodyId] : tileMap)
                removeAndDestroyBody(bodyInterface, bodyId);
        terrainBodies.clear();

        for (auto& [key, bodies] : vegetationBodies)
            for (auto& bodyId : bodies)
                removeAndDestroyBody(bodyInterface, bodyId);
        vegetationBodies.clear();
    }

    PhysicsTerrainManager::TileCoordKey PhysicsTerrainManager::makeTileKey(int32_t x, int32_t z)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32)
             | static_cast<uint64_t>(static_cast<uint32_t>(z));
    }

    JPH::BodyID PhysicsTerrainManager::addTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                                            const TerrainHeightFieldCreateInfo& info)
    {
        if (!ctx || !ctx->physicsSystem || !info.heightSamples || info.sampleCount == 0)
            return JPH::BodyID();

        JPH::HeightFieldShapeSettings shapeSettings(
            info.heightSamples,
            JPH::Vec3(info.offset.x, info.offset.y, info.offset.z),
            JPH::Vec3(info.scale.x, info.scale.y, info.scale.z),
            info.sampleCount);

        auto shapeResult = shapeSettings.Create();
        if (!shapeResult.IsValid())
        {
            vfLogError("Failed to create HeightFieldShape for terrain tile ({}, {}): {}",
                        tileX, tileZ, shapeResult.GetError().c_str());
            return JPH::BodyID();
        }

        JPH::BodyCreationSettings bodySettings(
            shapeResult.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
            JPH::EMotionType::Static, static_cast<JPH::ObjectLayer>(info.collisionLayer));

        bodySettings.mFriction = info.friction;
        bodySettings.mRestitution = info.restitution;
        bodySettings.mUserData = entityId;

        auto& bodyInterface = ctx->getBodyInterface();
        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);

        if (!bodyId.IsInvalid())
        {
            terrainBodies[entityId][makeTileKey(tileX, tileZ)] = bodyId;
        }

        return bodyId;
    }

    void PhysicsTerrainManager::removeTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ)
    {
        if (!ctx || !ctx->physicsSystem) return;

        TileCoordKey key = makeTileKey(tileX, tileZ);

        auto entityIt = terrainBodies.find(entityId);
        if (entityIt == terrainBodies.end()) return;

        auto tileIt = entityIt->second.find(key);
        if (tileIt == entityIt->second.end()) return;

        JPH::BodyID bodyId = tileIt->second;
        removeAndDestroyBody(ctx->getBodyInterface(), bodyId);

        entityIt->second.erase(tileIt);
        if (entityIt->second.empty()) terrainBodies.erase(entityIt);

        // Update memory tracking
        auto infoIt = colliderStreamInfos.find(key);
        if (infoIt != colliderStreamInfos.end())
        {
            currentPhysicsMemory -= std::min(currentPhysicsMemory, infoIt->second.memoryUsage);
            infoIt->second.currentLOD = 255;
            infoIt->second.memoryUsage = 0;
        }
    }

    void PhysicsTerrainManager::removeAllTerrainBodies(uint64_t entityId)
    {
        if (!ctx || !ctx->physicsSystem) return;

        auto& bodyInterface = ctx->getBodyInterface();

        auto entityIt = terrainBodies.find(entityId);
        if (entityIt != terrainBodies.end())
        {
            for (auto& [tileKey, bodyId] : entityIt->second)
            {
                removeAndDestroyBody(bodyInterface, bodyId);

                auto infoIt = colliderStreamInfos.find(tileKey);
                if (infoIt != colliderStreamInfos.end())
                {
                    currentPhysicsMemory -= std::min(currentPhysicsMemory, infoIt->second.memoryUsage);
                    colliderStreamInfos.erase(infoIt);
                }

                pendingColliderKeys.erase(tileKey);
            }
            terrainBodies.erase(entityIt);
        }

        // Also remove all cave bodies for this entity
        auto caveIt = caveBodies.find(entityId);
        if (caveIt != caveBodies.end())
        {
            for (auto& [tileKey, bodyId] : caveIt->second)
            {
                removeAndDestroyBody(bodyInterface, bodyId);
            }
            caveBodies.erase(caveIt);
        }
    }

    bool PhysicsTerrainManager::hasTerrainBodies(uint64_t entityId) const
    {
        auto it = terrainBodies.find(entityId);
        return it != terrainBodies.end() && !it->second.empty();
    }

    JPH::BodyID PhysicsTerrainManager::addStaticCapsule(const glm::vec3& position, float yRotation, float scale,
                                                          float radius, float height, uint8_t collisionLayer)
    {
        if (!ctx || !ctx->physicsSystem) return JPH::BodyID();

        float scaledRadius = radius * scale;
        float scaledHeight = height * scale;
        float halfHeight = scaledHeight * 0.5f - scaledRadius;

        constexpr float MIN_DIM = 0.001f;
        if (halfHeight < MIN_DIM)
        {
            halfHeight = MIN_DIM;
            scaledRadius = std::max(MIN_DIM, scaledHeight * 0.5f - MIN_DIM);
        }

        JPH::Ref<JPH::Shape> shape = new JPH::CapsuleShape(halfHeight, scaledRadius);

        glm::vec3 capsuleCenter = position + glm::vec3(0.0f, scaledHeight * 0.5f, 0.0f);

        JPH::Quat rot = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), yRotation);

        JPH::BodyCreationSettings settings(
            shape, toJoltR(capsuleCenter), rot,
            JPH::EMotionType::Static,
            static_cast<JPH::ObjectLayer>(collisionLayer));

        settings.mFriction = 0.5f;
        settings.mRestitution = 0.0f;

        auto& bodyInterface = ctx->getBodyInterface();
        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        return bodyId;
    }

    void PhysicsTerrainManager::addVegetationTileColliders(int32_t tileX, int32_t tileZ,
                                                             const std::vector<JPH::BodyID>& bodyIds)
    {
        TileCoordKey key = makeTileKey(tileX, tileZ);

        auto it = vegetationBodies.find(key);
        if (it != vegetationBodies.end())
        {
            if (ctx && ctx->physicsSystem)
            {
                auto& bodyInterface = ctx->getBodyInterface();
                for (auto& oldId : it->second)
                {
                    if (!oldId.IsInvalid())
                    {
                        removeAndDestroyBody(bodyInterface, oldId);
                    }
                }
            }
            it->second = bodyIds;
        }
        else
        {
            vegetationBodies[key] = bodyIds;
        }
    }

    void PhysicsTerrainManager::removeVegetationTileColliders(int32_t tileX, int32_t tileZ)
    {
        if (!ctx || !ctx->physicsSystem) return;

        TileCoordKey key = makeTileKey(tileX, tileZ);
        auto it = vegetationBodies.find(key);
        if (it == vegetationBodies.end()) return;

        auto& bodyInterface = ctx->getBodyInterface();
        for (auto& bodyId : it->second)
        {
            if (!bodyId.IsInvalid())
            {
                removeAndDestroyBody(bodyInterface, bodyId);
            }
        }
        vegetationBodies.erase(it);
    }

    void PhysicsTerrainManager::removeAllVegetationColliders()
    {
        if (!ctx || !ctx->physicsSystem) return;

        auto& bodyInterface = ctx->getBodyInterface();
        for (auto& [key, bodies] : vegetationBodies)
        {
            for (auto& bodyId : bodies)
            {
                if (!bodyId.IsInvalid())
                {
                    removeAndDestroyBody(bodyInterface, bodyId);
                }
            }
        }
        vegetationBodies.clear();
    }

    JPH::BodyID PhysicsTerrainManager::createFoliageStaticBody(const FoliageColliderCreateInfo& info)
    {
        if (!ctx || !ctx->physicsSystem) return JPH::BodyID();

        // Capsule reuses the existing helper (uniform scale, base-anchored Y offset) — ideal for
        // tree trunks: radius from the horizontal extent, height from the vertical extent.
        if (info.shape == types::ColliderShape::Capsule)
        {
            const float uniformScale = std::max({info.scale.x, info.scale.y, info.scale.z});
            const float radius = std::max(info.localAabbHalfExtents.x, info.localAabbHalfExtents.z);
            const float height = info.localAabbHalfExtents.y * 2.0f;
            return addStaticCapsule(info.position, info.yRotation, uniformScale, radius, height,
                                    info.collisionLayer);
        }

        // Box / ConvexMesh: build (or fetch cached) shape via the factory. ConvexMesh is cached by
        // meshPath so the heavy hull is built once per type; Box is a cheap primitive.
        ColliderCreateInfo ci;
        ci.shape = info.shape;
        ci.meshPath = info.meshPath;
        ci.halfExtents = info.localAabbHalfExtents * info.scale; // Box: pre-scaled dims
        ci.collisionLayer = info.collisionLayer;

        JPH::Ref<JPH::Shape> shape = PhysicsShapeFactory::createShape(ci);
        if (shape == nullptr) return JPH::BodyID();

        glm::vec3 bodyPos = info.position;
        if (info.shape == types::ColliderShape::ConvexMesh)
        {
            // Hull vertices carry their mesh-local offset; apply per-instance scale via a light
            // ScaledShape wrapper (the underlying hull stays shared/cached).
            if (info.scale != glm::vec3(1.0f))
                shape = new JPH::ScaledShape(shape, toJolt(info.scale));
        }
        else // Box: primitive is centered at the shape origin, so offset it to the mesh AABB center.
        {
            const glm::vec3 scaledCenter = info.localAabbCenter * info.scale;
            const glm::quat q = glm::angleAxis(info.yRotation, glm::vec3(0.0f, 1.0f, 0.0f));
            bodyPos = info.position + (q * scaledCenter);
        }

        const JPH::Quat rot = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), info.yRotation);
        JPH::BodyCreationSettings settings(
            shape, toJoltR(bodyPos), rot,
            JPH::EMotionType::Static, static_cast<JPH::ObjectLayer>(info.collisionLayer));
        settings.mFriction = 0.5f;
        settings.mRestitution = 0.0f;

        auto& bodyInterface = ctx->getBodyInterface();
        return bodyInterface.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    }

    void PhysicsTerrainManager::destroyStaticBody(JPH::BodyID bodyId)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;
        auto& bodyInterface = ctx->getBodyInterface();
        removeAndDestroyBody(bodyInterface, bodyId);
    }

    // --- Async streaming pipeline ---

    JPH::BodyID PhysicsTerrainManager::addTerrainTileBodyFromShape(
        uint64_t entityId, int32_t tileX, int32_t tileZ,
        JPH::Ref<JPH::Shape> shape, float friction, float restitution, uint8_t collisionLayer)
    {
        if (!ctx || !ctx->physicsSystem || shape == nullptr)
            return JPH::BodyID();

        JPH::BodyCreationSettings bodySettings(
            shape, JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
            JPH::EMotionType::Static, static_cast<JPH::ObjectLayer>(collisionLayer));

        bodySettings.mFriction = friction;
        bodySettings.mRestitution = restitution;
        bodySettings.mUserData = entityId;

        auto& bodyInterface = ctx->getBodyInterface();
        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);

        if (!bodyId.IsInvalid())
            terrainBodies[entityId][makeTileKey(tileX, tileZ)] = bodyId;

        return bodyId;
    }

    void PhysicsTerrainManager::submitAsyncCollider(OwnedTerrainColliderData data)
    {
        TileCoordKey key = makeTileKey(data.tileX, data.tileZ);

        // Cancel any existing pending collider for this tile
        cancelPendingCollider(data.tileX, data.tileZ);

        // NOTE: Do NOT remove the existing body here. The old collider stays active
        // until pollColliderCompletions promotes the replacement, preventing a physics
        // gap where objects could fall through during async shape creation.

        // Decimate based on physics LOD
        auto [decimatedSamples, decimatedCount] = decimateHeightField(
            data.heightSamples.data(), data.sampleCount, data.physicsLOD);

        size_t estMemory = estimatePhysicsTileMemory(data.sampleCount, data.physicsLOD);

        // Build shape offset and scale from world origin and vertex spacing
        float totalSize = (decimatedCount - 1) * data.vertexSpacing *
                          (data.physicsLOD == 0 ? 1.0f : static_cast<float>(1u << data.physicsLOD));
        glm::vec3 offset = data.worldOrigin;
        glm::vec3 scale{totalSize / static_cast<float>(decimatedCount - 1), 1.0f,
                         totalSize / static_cast<float>(decimatedCount - 1)};

        PendingPhysicsCollider pending;
        pending.entityId = data.entityId;
        pending.tileX = data.tileX;
        pending.tileZ = data.tileZ;
        pending.physicsLOD = data.physicsLOD;
        pending.estimatedMemory = estMemory;
        pending.worldOrigin = data.worldOrigin;
        pending.vertexSpacing = data.vertexSpacing;
        pending.decimatedSampleCount = decimatedCount;
        pending.friction = data.friction;
        pending.restitution = data.restitution;
        pending.collisionLayer = data.collisionLayer;

        // Launch async shape creation — HeightFieldShapeSettings::Create() is pure math, thread-safe
        auto capturedOffset = offset;
        auto capturedScale = scale;
        pending.shapeFuture = std::async(std::launch::async,
            [samples = std::move(decimatedSamples), count = decimatedCount,
             capturedOffset, capturedScale]() -> JPH::Ref<JPH::Shape>
            {
                JPH::HeightFieldShapeSettings shapeSettings(
                    samples.data(),
                    JPH::Vec3(capturedOffset.x, capturedOffset.y, capturedOffset.z),
                    JPH::Vec3(capturedScale.x, capturedScale.y, capturedScale.z),
                    count);

                auto result = shapeSettings.Create();
                if (!result.IsValid())
                    return nullptr;

                return result.Get();
            });

        pendingMemoryReserved += estMemory;
        pendingColliderKeys.insert(key);

        // Cache full-res data for LOD transitions
        auto& streamInfo = colliderStreamInfos[key];
        streamInfo.entityId = data.entityId;
        streamInfo.tileX = data.tileX;
        streamInfo.tileZ = data.tileZ;
        streamInfo.currentLOD = 255; // not yet loaded
        streamInfo.cachedData = std::move(data);

        pendingColliders.push_back(std::move(pending));
    }

    void PhysicsTerrainManager::cancelPendingCollider(int32_t tileX, int32_t tileZ)
    {
        TileCoordKey key = makeTileKey(tileX, tileZ);
        pendingColliderKeys.erase(key);
        // The background thread still runs to completion — only the result is discarded
        // when pollColliderCompletions sees the key is no longer in pendingColliderKeys.
    }

    void PhysicsTerrainManager::pollColliderCompletions()
    {
        uint32_t created = 0;

        auto it = pendingColliders.begin();
        while (it != pendingColliders.end() && created < streamConfig.maxCreationsPerFrame)
        {
            if (!it->shapeFuture.valid())
            {
                pendingMemoryReserved -= std::min(pendingMemoryReserved, it->estimatedMemory);
                it = pendingColliders.erase(it);
                continue;
            }

            auto status = it->shapeFuture.wait_for(std::chrono::seconds(0));
            if (status != std::future_status::ready)
            {
                ++it;
                continue;
            }

            TileCoordKey key = makeTileKey(it->tileX, it->tileZ);
            pendingMemoryReserved -= std::min(pendingMemoryReserved, it->estimatedMemory);

            // Check if tile was cancelled (streamed out while shape was building)
            if (pendingColliderKeys.find(key) == pendingColliderKeys.end())
            {
                it = pendingColliders.erase(it);
                continue;
            }

            JPH::Ref<JPH::Shape> shape = it->shapeFuture.get();
            pendingColliderKeys.erase(key);

            if (shape != nullptr)
            {
                // Remove the old body (if any) right before adding the replacement,
                // so there is never a frame without a collider for this tile.
                removeTerrainTileBody(it->entityId, it->tileX, it->tileZ);

                JPH::BodyID bodyId = addTerrainTileBodyFromShape(
                    it->entityId, it->tileX, it->tileZ,
                    shape, it->friction, it->restitution, it->collisionLayer);

                if (!bodyId.IsInvalid())
                {
                    currentPhysicsMemory += it->estimatedMemory;

                    auto infoIt = colliderStreamInfos.find(key);
                    if (infoIt != colliderStreamInfos.end())
                    {
                        infoIt->second.currentLOD = it->physicsLOD;
                        infoIt->second.memoryUsage = it->estimatedMemory;
                        infoIt->second.lastAccessFrame = currentFrame;
                    }

                    ++created;
                }
            }
            else
            {
                vfLogWarning("PhysicsTerrainManager: Async shape creation failed for tile ({}, {})",
                              it->tileX, it->tileZ);
            }

            it = pendingColliders.erase(it);
        }
    }

    uint8_t PhysicsTerrainManager::selectPhysicsLOD(float distance) const
    {
        if (distance < streamConfig.lodDistances[0])
            return 0;
        if (distance < streamConfig.lodDistances[1])
            return 1;
        return 2;
    }

    void PhysicsTerrainManager::processEvictions()
    {
        size_t budget = static_cast<size_t>(
            static_cast<float>(streamConfig.memoryBudgetBytes) * streamConfig.evictionThreshold);

        if (currentPhysicsMemory <= budget)
            return;

        // Build eviction candidates sorted by distance (farthest first)
        struct EvictionCandidate
        {
            TileCoordKey key;
            uint64_t entityId;
            int32_t tileX, tileZ;
            float distance;
            size_t memory;
        };

        std::vector<EvictionCandidate> candidates;
        candidates.reserve(colliderStreamInfos.size());

        for (auto& [key, info] : colliderStreamInfos)
        {
            if (info.currentLOD == 255)
                continue; // not yet loaded

            candidates.push_back({key, info.entityId, info.tileX, info.tileZ,
                                   info.distanceToCamera, info.memoryUsage});
        }

        std::sort(candidates.begin(), candidates.end(),
                   [](const auto& a, const auto& b) { return a.distance > b.distance; });

        for (auto& c : candidates)
        {
            if (currentPhysicsMemory <= budget)
                break;

            // removeTerrainTileBody already decrements currentPhysicsMemory via its tracking block
            removeTerrainTileBody(c.entityId, c.tileX, c.tileZ);
        }
    }

    void PhysicsTerrainManager::checkLODTransitions(const glm::vec3& cameraPosition)
    {
        // Collect tiles needing LOD transition into a temp vector to avoid
        // iterator invalidation — submitAsyncCollider modifies colliderStreamInfos.
        struct LODTransition
        {
            OwnedTerrainColliderData data;
            uint8_t targetLOD;
        };
        std::vector<LODTransition> transitions;

        for (auto& [key, info] : colliderStreamInfos)
        {
            if (info.currentLOD == 255)
                continue; // not loaded

            // Reuse distanceToCamera already computed by updateColliderStreaming
            uint8_t targetLOD = selectPhysicsLOD(info.distanceToCamera);
            if (targetLOD != info.currentLOD && !info.cachedData.heightSamples.empty())
            {
                OwnedTerrainColliderData data = info.cachedData; // copy
                data.physicsLOD = targetLOD;
                transitions.push_back({std::move(data), targetLOD});
            }
        }

        for (auto& t : transitions)
            submitAsyncCollider(std::move(t.data));
    }

    void PhysicsTerrainManager::updateColliderStreaming(const glm::vec3& cameraPosition)
    {
        ++currentFrame;
        pollColliderCompletions();

        // Update distances for all tracked tiles (single pass, reused by checkLODTransitions)
        for (auto& [key, info] : colliderStreamInfos)
        {
            if (info.cachedData.heightSamples.empty())
                continue;

            glm::vec3 tileCenter = info.cachedData.worldOrigin +
                glm::vec3(info.cachedData.vertexSpacing * (info.cachedData.sampleCount - 1) * 0.5f,
                           0.0f,
                           info.cachedData.vertexSpacing * (info.cachedData.sampleCount - 1) * 0.5f);

            info.distanceToCamera = glm::length(cameraPosition - tileCenter);
            info.lastAccessFrame = currentFrame;
        }

        checkLODTransitions(cameraPosition);
        processEvictions();
    }

    void PhysicsTerrainManager::setStreamConfig(const PhysicsColliderStreamConfig& config)
    {
        streamConfig = config;
    }

    // --- Cave / Vegetation (unchanged) ---

    JPH::TriangleList PhysicsTerrainManager::buildJoltTriangleList(const services::CaveTileColliderInfo& cave)
    {
        JPH::TriangleList triangles;
        triangles.reserve(cave.indexCount / 3);

        for (uint32_t i = 0; i + 2 < cave.indexCount; i += 3)
        {
            const auto& v0 = cave.vertices[cave.indices[i]];
            const auto& v1 = cave.vertices[cave.indices[i + 1]];
            const auto& v2 = cave.vertices[cave.indices[i + 2]];

            triangles.push_back(JPH::Triangle(
                JPH::Float3(v0.x, v0.y, v0.z),
                JPH::Float3(v1.x, v1.y, v1.z),
                JPH::Float3(v2.x, v2.y, v2.z)));
        }

        return triangles;
    }

    void PhysicsTerrainManager::addCaveTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                                  const services::CaveTileColliderInfo& cave)
    {
        if (!ctx || !ctx->physicsSystem) return;
        if (!cave.vertices || cave.vertexCount == 0 || !cave.indices || cave.indexCount < 3) return;

        JPH::TriangleList triangles = buildJoltTriangleList(cave);
        if (triangles.empty()) return;

        JPH::MeshShapeSettings settings(triangles);
        auto result = settings.Create();
        if (result.HasError())
        {
            vfLogWarning("PhysicsTerrainManager: Failed to create cave mesh shape for tile ({}, {}): {}",
                          tileX, tileZ, result.GetError().c_str());
            return;
        }

        JPH::BodyCreationSettings bodySettings(
            result.Get(),
            JPH::RVec3::sZero(),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            static_cast<JPH::ObjectLayer>(cave.collisionLayer));

        bodySettings.mFriction = cave.friction;
        bodySettings.mRestitution = cave.restitution;

        auto& bodyInterface = ctx->getBodyInterface();
        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);

        if (bodyId.IsInvalid())
        {
            vfLogWarning("PhysicsTerrainManager: Failed to add cave body for tile ({}, {})", tileX, tileZ);
            return;
        }

        auto key = makeTileKey(tileX, tileZ);
        caveBodies[entityId][key] = bodyId;
    }

    void PhysicsTerrainManager::removeCaveTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ)
    {
        if (!ctx || !ctx->physicsSystem) return;

        auto entityIt = caveBodies.find(entityId);
        if (entityIt == caveBodies.end()) return;

        auto key = makeTileKey(tileX, tileZ);
        auto tileIt = entityIt->second.find(key);
        if (tileIt == entityIt->second.end()) return;

        auto& bodyInterface = ctx->getBodyInterface();
        if (!tileIt->second.IsInvalid())
        {
            removeAndDestroyBody(bodyInterface, tileIt->second);
        }

        entityIt->second.erase(tileIt);
        if (entityIt->second.empty())
            caveBodies.erase(entityIt);
    }
}
