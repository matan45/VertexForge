#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <array>
#include <cstdint>

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

        std::array<float, 4> lodDistances = { 100.0f, 300.0f, 600.0f, 1200.0f };

        std::string heightmapPath;
        std::string terrainMaterialPath;
        std::string weightMapPath;

        bool isActive = true;
        bool isDirty = false;

        uint32_t activeTileCount = 0;
        uint32_t visibleTileCount = 0;

        std::string savePath;
        bool saveDirty = false;
    };

    struct TerrainTileComponent
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;

        uint8_t currentLOD = 0;
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
