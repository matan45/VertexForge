#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include "../asset/AssetRef.hpp"
#include "../terrain/HeightmapLoader.hpp"

namespace components
{
    struct TerrainComponent
    {
        uint8_t resolution = 0;
        float worldTileSize = 32.0f;
        float maxHeight = 100.0f;
        float minHeight = -10.0f;

        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;

        std::string heightmapPath;
        std::vector<terrain::HeightmapRegion> heightmapRegions;
        asset::AssetRef terrainMaterialRef;
        std::string weightMapPath;

        // VK-1614 world-anchored wetness/snow mask (R = wetness, G = snow), stored as a `.vfImage`.
        //
        // surfaceMaskWorldRect is (minX, minZ, maxX, maxZ) in WORLD space and is AUTHORED — snapshotted
        // from the terrain's bounds when the mask is created and never recomputed. That is deliberate
        // and load-bearing: TileCoord is signed and the tile map is sparse and mutable at runtime
        // (TerrainGrid::addTileFromFile is the streaming path), so a rect derived from live grid bounds
        // would slide and rescale every painted puddle the moment a tile appeared at a negative coord.
        // Terrain outside the rect samples 0 and falls back to global-only weather, which degrades
        // gracefully instead of corrupting existing paint.
        std::string surfaceMaskPath;
        glm::vec4 surfaceMaskWorldRect{0.0f};
        uint32_t surfaceMaskResolution = 1024;

        bool isActive = true;
        bool isDirty = false;

        uint32_t activeTileCount = 0;
        uint32_t visibleTileCount = 0;

        asset::AssetRef terrainRef;
        std::string savePath;
        bool saveDirty = false;
    };

    struct TerrainTileComponent
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;

        bool isVisible = true;

        bool isDirty = false;
        bool isGPUResident = false;

        float boundingMinY = 0.0f;
        float boundingMaxY = 0.0f;
    };

    struct TerrainColliderComponent
    {
        bool hasCollider = false;
        uint8_t collisionLayer = 0;
        float friction = 0.5f;
        float restitution = 0.0f;
    };

    struct TerrainColliderDebugData
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> lineIndices;
        uint32_t version = 0;
    };

    struct TerrainTileColliderDebugComponent
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        TerrainColliderDebugData debugData;
    };
}
