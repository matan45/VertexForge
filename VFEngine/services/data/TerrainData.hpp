#pragma once
#include <string>
#include <array>
#include <cstdint>

namespace services
{
    struct TerrainCreationData
    {
        int32_t tilesX = 4;
        int32_t tilesZ = 4;

        uint8_t resolution = 0;  // 0=Low(33x33), 1=Medium(65x65), 2=High(129x129), 3=Ultra(257x257)
        float worldTileSize = 32.0f;

        float maxHeight = 100.0f;
        float minHeight = -10.0f;

        std::array<float, 4> lodDistances = { 100.0f, 300.0f, 600.0f, 1200.0f };

        std::string heightmapPath;  // empty = flat terrain
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
        uint32_t tileCount = 0;

        bool isActive = true;
        bool isDirty = false;
        uint32_t activeTileCount = 0;
        uint32_t visibleTileCount = 0;
    };

    struct TerrainTileData
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
}
