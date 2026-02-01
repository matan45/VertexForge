#pragma once
#include <string>
#include <array>
#include <cstdint>

namespace services
{
    // Data for creating a new terrain
    struct TerrainCreationData
    {
        // Grid size (number of tiles)
        int32_t tilesX = 4;
        int32_t tilesZ = 4;

        // Tile configuration
        uint8_t resolution = 0;  // 0=Low(33x33), 1=Medium(65x65), 2=High(129x129), 3=Ultra(257x257)
        float worldTileSize = 32.0f;

        // Height range
        float maxHeight = 100.0f;
        float minHeight = -10.0f;

        // LOD distances
        std::array<float, 4> lodDistances = { 100.0f, 300.0f, 600.0f, 1200.0f };

        // Optional heightmap file path (empty = flat terrain)
        std::string heightmapPath;
    };

    // Read-only terrain data for queries
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
    };
}
