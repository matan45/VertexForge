#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>

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

    class PhysicsTerrainManager
    {
    public:
        using TileCoordKey = uint64_t;

        void init(PhysicsContext* context);
        void cleanUp();

        JPH::BodyID addTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ,
                                        const TerrainHeightFieldCreateInfo& info);
        void removeTerrainTileBody(uint64_t entityId, int32_t tileX, int32_t tileZ);
        void removeAllTerrainBodies(uint64_t entityId);
        bool hasTerrainBodies(uint64_t entityId) const;

        JPH::BodyID addStaticCapsule(const glm::vec3& position, float yRotation, float scale,
                                      float radius, float height, uint8_t collisionLayer = 0);
        void addVegetationTileColliders(int32_t tileX, int32_t tileZ,
                                         const std::vector<JPH::BodyID>& bodyIds);
        void removeVegetationTileColliders(int32_t tileX, int32_t tileZ);
        void removeAllVegetationColliders();

        static TileCoordKey makeTileKey(int32_t x, int32_t z);

    private:
        PhysicsContext* ctx = nullptr;
        std::unordered_map<uint64_t, std::unordered_map<TileCoordKey, JPH::BodyID>> terrainBodies;
        std::unordered_map<TileCoordKey, std::vector<JPH::BodyID>> vegetationBodies;
    };
}
