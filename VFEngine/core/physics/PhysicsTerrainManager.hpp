#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <glm/glm.hpp>
#include "providers/physics/IPhysicsProvider.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <future>

namespace core::physics
{
    struct PhysicsContext;

    struct TerrainHeightFieldCreateInfo
    {
        const float* heightSamples = nullptr;
        uint32_t sampleCount = 0;
        glm::vec3 offset{0.0f};
        glm::vec3 scale{1.0f};
        float friction = 0.5f;
        float restitution = 0.0f;
        uint8_t collisionLayer = 0;
    };

    struct PhysicsColliderStreamConfig
    {
        size_t memoryBudgetBytes = 64 * 1024 * 1024; // 64 MB
        float evictionThreshold = 0.9f;
        uint32_t maxCreationsPerFrame = 4;
        float lodDistances[3] = {100.0f, 300.0f, 600.0f}; // full, half, quarter
    };

    struct OwnedTerrainColliderData
    {
        uint64_t entityId = 0;
        int32_t tileX = 0;
        int32_t tileZ = 0;
        std::vector<float> heightSamples; // owned copy
        uint32_t sampleCount = 0;
        glm::vec3 worldOrigin{0.0f};
        float vertexSpacing = 1.0f;
        float friction = 0.5f;
        float restitution = 0.0f;
        uint8_t collisionLayer = 0;
        uint8_t physicsLOD = 0;
    };

    struct PendingPhysicsCollider
    {
        uint64_t entityId = 0;
        int32_t tileX = 0;
        int32_t tileZ = 0;
        uint8_t physicsLOD = 0;
        std::future<JPH::Ref<JPH::Shape>> shapeFuture;
        size_t estimatedMemory = 0;
        glm::vec3 worldOrigin{0.0f};
        float vertexSpacing = 1.0f;
        uint32_t decimatedSampleCount = 0;
        float friction = 0.5f;
        float restitution = 0.0f;
        uint8_t collisionLayer = 0;
    };

    struct PhysicsColliderStreamInfo
    {
        uint64_t entityId = 0;
        int32_t tileX = 0;
        int32_t tileZ = 0;
        uint8_t currentLOD = 255;
        float distanceToCamera = 0.0f;
        size_t memoryUsage = 0;
        uint64_t lastAccessFrame = 0;
        OwnedTerrainColliderData cachedData; // full-res cache for LOD transitions
    };

    // VK-1584 — one entity-free proximity foliage collider. The Jolt shape (convex hull cached by
    // meshPath, or a box/capsule sized from the mesh AABB) is shared across every instance of a
    // FoliageType; only the transform varies per instance.
    struct FoliageColliderCreateInfo
    {
        types::ColliderShape shape = types::ColliderShape::Capsule;
        std::string meshPath;                 // ConvexMesh source (loaded + cached by meshPath)
        glm::vec3 position{0.0f};             // instance world position (mesh origin / base)
        float yRotation = 0.0f;
        glm::vec3 scale{1.0f};                // per-instance scale
        glm::vec3 localAabbCenter{0.0f};      // mesh-local AABB center (Box/Capsule offset)
        glm::vec3 localAabbHalfExtents{0.5f}; // mesh-local AABB half-size (Box/Capsule sizing)
        uint8_t collisionLayer = 0;
    };

    class PhysicsTerrainManager
    {
    public:
        using TileCoordKey = uint64_t;

        void init(PhysicsContext* context);
        void cleanUp();

        // Synchronous body creation (used for brush rebuilds)
        JPH::BodyID addTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                        const TerrainHeightFieldCreateInfo& info);
        void removeTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ);
        void removeAllTerrainBodies(uint64_t entityId);
        bool hasTerrainBodies(uint64_t entityId) const;

        // Async collider streaming pipeline
        JPH::BodyID addTerrainTileBodyFromShape(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                                  JPH::Ref<JPH::Shape> shape,
                                                  float friction, float restitution, uint8_t collisionLayer);
        void submitAsyncCollider(OwnedTerrainColliderData data);
        void cancelPendingCollider(int32_t tileX, int32_t tileZ);
        void updateColliderStreaming(const glm::vec3& cameraPosition);
        uint8_t selectPhysicsLOD(float distance) const;

        void setStreamConfig(const PhysicsColliderStreamConfig& config);
        const PhysicsColliderStreamConfig& getStreamConfig() const { return streamConfig; }

        void addCaveTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                              const services::CaveTileColliderInfo& cave);
        void removeCaveTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ);

        JPH::BodyID addStaticCapsule(const glm::vec3& position, float yRotation, float scale,
                                      float radius, float height, uint8_t collisionLayer = 0);
        void addVegetationTileColliders(int32_t tileX, int32_t tileZ,
                                         const std::vector<JPH::BodyID>& bodyIds);
        void removeVegetationTileColliders(int32_t tileX, int32_t tileZ);
        void removeAllVegetationColliders();

        // VK-1584 — proximity foliage colliders: entity-free static body from a per-type shared
        // shape; caller tracks the returned BodyID and destroys it via destroyStaticBody.
        JPH::BodyID createFoliageStaticBody(const FoliageColliderCreateInfo& info);
        void destroyStaticBody(JPH::BodyID bodyId);

        static TileCoordKey makeTileKey(int32_t x, int32_t z);

    private:
        void pollColliderCompletions();
        void processEvictions();
        void checkLODTransitions(const glm::vec3& cameraPosition);

        static JPH::TriangleList buildJoltTriangleList(const services::CaveTileColliderInfo& cave);

        PhysicsContext* ctx = nullptr;
        std::unordered_map<uint64_t, std::unordered_map<TileCoordKey, JPH::BodyID>> terrainBodies;
        std::unordered_map<uint64_t, std::unordered_map<TileCoordKey, JPH::BodyID>> caveBodies;
        std::unordered_map<TileCoordKey, std::vector<JPH::BodyID>> vegetationBodies;

        // Async streaming state
        std::vector<PendingPhysicsCollider> pendingColliders;
        std::unordered_set<TileCoordKey> pendingColliderKeys;
        std::unordered_map<TileCoordKey, PhysicsColliderStreamInfo> colliderStreamInfos;
        size_t currentPhysicsMemory = 0;
        size_t pendingMemoryReserved = 0;
        uint64_t currentFrame = 0;
        PhysicsColliderStreamConfig streamConfig;
    };
}
