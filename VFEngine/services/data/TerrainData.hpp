#pragma once
#include "EntityHandle.hpp"
#include "terrain/HeightmapLoader.hpp"
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <optional>

namespace services
{
    struct TerrainCreationData
    {
        int32_t tilesX = 4;
        int32_t tilesZ = 4;

        uint8_t resolution = 0;
        float worldTileSize = 32.0f;

        float maxHeight = 100.0f;
        float minHeight = -10.0f;

        std::string heightmapPath;
        std::vector<terrain::HeightmapRegion> heightmapRegions;
        std::string terrainMaterialPath;
        std::string weightMapPath;
    };

    struct TerrainCreationPollResult
    {
        bool inProgress = false;
        float progress = 0.0f;
        std::string stage;
        std::optional<EntityHandle> result;
    };

    struct TerrainData
    {
        uint8_t resolution = 0;
        float worldTileSize = 0.0f;
        float maxHeight = 0.0f;
        float minHeight = 0.0f;
        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;
        std::string heightmapPath;
        std::vector<terrain::HeightmapRegion> heightmapRegions;
        std::string terrainMaterialPath;
        std::string weightMapPath;
        uint32_t tileCount = 0;

        bool isActive = true;
        bool isDirty = false;
        uint32_t activeTileCount = 0;
        uint32_t visibleTileCount = 0;

        std::string savePath;
        bool saveDirty = false;

        // Collider properties (from TerrainColliderComponent)
        uint8_t colliderCollisionLayer = 0;
        float colliderFriction = 0.5f;
        float colliderRestitution = 0.0f;
    };

    struct TerrainTileData
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        bool isVisible = true;
        bool isDirty = false;
        bool isGPUResident = false;
        float boundingMinY = 0.0f;
        float boundingMaxY = 0.0f;
    };
}
