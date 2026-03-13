#include "PhysicsTerrainManager.hpp"
#include "PhysicsContext.hpp"
#include "JoltConversions.hpp"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include "print/Log.hpp"

namespace core::physics
{
    void PhysicsTerrainManager::init(PhysicsContext* context)
    {
        ctx = context;
    }

    void PhysicsTerrainManager::cleanUp()
    {
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

        auto entityIt = terrainBodies.find(entityId);
        if (entityIt == terrainBodies.end()) return;

        auto tileIt = entityIt->second.find(makeTileKey(tileX, tileZ));
        if (tileIt == entityIt->second.end()) return;

        JPH::BodyID bodyId = tileIt->second;
        removeAndDestroyBody(ctx->getBodyInterface(), bodyId);

        entityIt->second.erase(tileIt);
        if (entityIt->second.empty()) terrainBodies.erase(entityIt);
    }

    void PhysicsTerrainManager::removeAllTerrainBodies(uint64_t entityId)
    {
        if (!ctx || !ctx->physicsSystem) return;

        auto entityIt = terrainBodies.find(entityId);
        if (entityIt == terrainBodies.end()) return;

        auto& bodyInterface = ctx->getBodyInterface();
        for (auto& [tileKey, bodyId] : entityIt->second)
        {
            removeAndDestroyBody(bodyInterface, bodyId);
        }

        terrainBodies.erase(entityIt);
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
}
