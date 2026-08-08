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

    // A sector id packs each axis into 16 bits, so a coord is only uniquely addressable over a
    // 65536-wide window per axis. Biasing by 0x8000 centres that window on the origin - the full
    // int16 range. Outside it two distinct sectors collapse onto the same id and silently share a
    // GPU streaming slot, so validate with isValidSectorCoord() before creating a sector.
    inline constexpr int32_t kMinSectorCoord = -32768;
    inline constexpr int32_t kMaxSectorCoord = 32767;

    [[nodiscard]] inline constexpr bool isValidSectorCoord(const SectorCoord& coord) noexcept
    {
        return coord.x >= kMinSectorCoord && coord.x <= kMaxSectorCoord
            && coord.z >= kMinSectorCoord && coord.z <= kMaxSectorCoord;
    }

    // Bit concatenation (not a pairing function): biased x in the high 16 bits, biased z in the
    // low 16. Injective exactly over [kMinSectorCoord, kMaxSectorCoord] on both axes.
    //
    // VK-1588 re-biased this from 0x4000, whose window was the lopsided [-16384, +49151]. Safe
    // because the id is runtime-only: .vfworld/.vfsector key sectors on {x,z}, .vfsector filenames
    // and .vfHLOD paths are coord-derived, and .vfpak is path-keyed. The only consumers are the
    // transient in-memory maps in LightStreamManager and GPUObjectStreamManager.
    //
    // The int32 -> uint32 cast happens BEFORE the bias so an out-of-range coord wraps under
    // defined unsigned arithmetic instead of overflowing a signed int (UB).
    [[nodiscard]] inline uint32_t sectorCoordToId(const SectorCoord& coord) noexcept
    {
        const uint32_t ux = (static_cast<uint32_t>(coord.x) + 0x8000u) & 0xFFFFu;
        const uint32_t uz = (static_cast<uint32_t>(coord.z) + 0x8000u) & 0xFFFFu;
        return (ux << 16) | uz;
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
        bool editModeStreaming = false;     // Run the streaming ring off the editor camera in edit mode

        // HLOD distance tiers (in sector counts, beyond unloadRadius)
        float hlodTier0Radius = 10.0f;
        float hlodTier1Radius = 20.0f;
        float hlodTier2Radius = 40.0f;
    };

} // namespace world
