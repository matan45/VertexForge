#pragma once

#include <cstdint>
#include <functional>

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
    };

    struct SectorStreamingConfig
    {
        float loadRadius = 512.0f;
        float unloadRadius = 640.0f;
        int maxLoadsPerFrame = 1;
        int maxUnloadsPerFrame = 1;
        int maxEntitiesPerFrame = 8;
    };

} // namespace world
