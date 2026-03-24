#pragma once

#include <cstdint>
#include <cmath>
#include <functional>
#include <glm/glm.hpp>

namespace world
{
    struct SectorCoord
    {
        int32_t x = 0;
        int32_t z = 0;

        SectorCoord() = default;
        SectorCoord(int32_t x, int32_t z) : x(x), z(z) {}

        bool operator==(const SectorCoord& other) const
        {
            return x == other.x && z == other.z;
        }

        bool operator!=(const SectorCoord& other) const
        {
            return !(*this == other);
        }

        SectorCoord operator+(const SectorCoord& other) const
        {
            return SectorCoord(x + other.x, z + other.z);
        }

        SectorCoord operator-(const SectorCoord& other) const
        {
            return SectorCoord(x - other.x, z - other.z);
        }
    };

    struct SectorCoordHash
    {
        size_t operator()(const SectorCoord& coord) const
        {
            size_t h1 = std::hash<int32_t>{}(coord.x);
            size_t h2 = std::hash<int32_t>{}(coord.z);
            return h1 ^ (h2 << 1);
        }
    };

    inline uint32_t sectorCoordToId(const SectorCoord& coord)
    {
        // Encode two int32s into a single uint32 via Cantor-style pairing
        // Shift to unsigned range first (offset by 0x4000 to support negative coords)
        auto ux = static_cast<uint32_t>(coord.x + 0x4000);
        auto uz = static_cast<uint32_t>(coord.z + 0x4000);
        return (ux << 16) | (uz & 0xFFFF);
    }

    enum class SectorState : uint8_t
    {
        Unloaded = 0,
        Loading,
        Loaded,
        Unloading
    };

    struct SectorConfig
    {
        float sectorWorldSize = 128.0f;
        int32_t tilesPerSector = 4;
        bool alignedToTerrain = false;
    };

    inline SectorConfig alignSectorConfigToTerrain(float worldTileSize, int32_t tilesPerSector)
    {
        SectorConfig config;
        config.tilesPerSector = tilesPerSector;
        config.sectorWorldSize = worldTileSize * static_cast<float>(tilesPerSector);
        config.alignedToTerrain = true;
        return config;
    }

    inline bool isSectorAlignedToTerrain(const SectorConfig& config, float worldTileSize)
    {
        float expected = worldTileSize * static_cast<float>(config.tilesPerSector);
        return std::abs(config.sectorWorldSize - expected) < 0.001f;
    }

    struct StreamingSource
    {
        glm::vec3 position{0.0f};
        float radiusMultiplier = 1.0f;
        uint8_t priority = 0;
        uint32_t id = 0;
    };

    struct SectorStreamingConfig
    {
        float loadRadius = 4.0f;    // in sector counts (e.g. 4 = load sectors within 4 sectors of camera)
        float unloadRadius = 5.0f;  // in sector counts (should be > loadRadius for hysteresis)
        int maxLoadsPerFrame = 1;
        int maxUnloadsPerFrame = 1;
        int maxEntitiesPerFrame = 8;
        int maxTerrainLoadsPerFrame = 4;    // terrain tiles loaded per frame via sector activation
        int maxTerrainUnloadsPerFrame = 4;  // terrain tiles unloaded per frame via sector deactivation
        bool enableGPUObjectStreaming = true; // Use persistent GPU slots with priority-based streaming
    };

} // namespace world
