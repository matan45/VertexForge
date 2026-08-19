#pragma once

#include "WorldTypes.hpp"
#include "../math/Frustum.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace world
{
    struct HLODCellCoord
    {
        int32_t x = 0;
        int32_t z = 0;
        uint8_t tier = 0;

        HLODCellCoord() = default;
        constexpr HLODCellCoord(int32_t x, int32_t z, uint8_t tier) : x(x), z(z), tier(tier) {}

        bool operator==(const HLODCellCoord& other) const
        {
            return x == other.x && z == other.z && tier == other.tier;
        }

        bool operator!=(const HLODCellCoord& other) const
        {
            return !(*this == other);
        }
    };

    struct HLODCellCoordHash
    {
        size_t operator()(const HLODCellCoord& coord) const
        {
            size_t h1 = std::hash<int32_t>{}(coord.x);
            size_t h2 = std::hash<int32_t>{}(coord.z);
            size_t h3 = std::hash<uint8_t>{}(coord.tier);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct HLODTierConfig
    {
        uint8_t tier = 0;
        uint8_t cellSize = 1;              // sectors per cell edge: 1, 2, 4
        float displayRadius = 10.0f;       // in sector counts, beyond unloadRadius
        float simplificationRatio = 0.1f;  // target triangle ratio vs source
    };

    struct HLODConfig
    {
        bool enabled = false;
        std::vector<HLODTierConfig> tiers;

        static HLODConfig defaultConfig()
        {
            HLODConfig config;
            config.enabled = false;
            config.tiers = {
                {0, 1, 10.0f, 0.10f},
                {1, 2, 20.0f, 0.03f},
                {2, 4, 40.0f, 0.01f}
            };
            return config;
        }
    };

    // A cellSize of 0 would divide by zero below. .vfworld stores the value verbatim, so clamp
    // once here rather than trusting every writer of the tier table.
    [[nodiscard]] inline constexpr int32_t effectiveCellSize(const HLODTierConfig& tier) noexcept
    {
        return tier.cellSize > 0 ? static_cast<int32_t>(tier.cellSize) : 1;
    }

    // Floor division, NOT truncation: the cell index containing sector index v. Truncation folds
    // -1 and 0 into cell 0 at cellSize 2, so a negative-coord sector would be baked into the wrong
    // cell and streamed from a different one.
    [[nodiscard]] inline constexpr int32_t floorDivCell(int32_t v, int32_t cellSize) noexcept
    {
        return (v >= 0) ? v / cellSize : (v - cellSize + 1) / cellSize;
    }

    // Single source of truth for sector -> cell mapping, shared by the bake planner, the streamer
    // and HLOD invalidation.
    [[nodiscard]] inline constexpr HLODCellCoord sectorToCell(const SectorCoord& coord,
                                                              const HLODTierConfig& tier) noexcept
    {
        const int32_t cs = effectiveCellSize(tier);
        return HLODCellCoord(floorDivCell(coord.x, cs), floorDivCell(coord.z, cs), tier.tier);
    }

    // The origin sector of a cell (its minimum corner), the inverse of sectorToCell.
    [[nodiscard]] inline constexpr SectorCoord cellOriginSector(const HLODCellCoord& cell,
                                                                const HLODTierConfig& tier) noexcept
    {
        const int32_t cs = effectiveCellSize(tier);
        return SectorCoord(cell.x * cs, cell.z * cs);
    }

    struct HLODProxySubmesh
    {
        std::string materialPath;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t meshletCount = 0;
    };

    struct HLODProxyInfo
    {
        HLODCellCoord cellCoord;
        std::string hlodFilePath;
        math::AABB bounds;
        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;
        uint32_t submeshCount = 0;
        bool generated = false;
    };

    enum class HLODProxyState : uint8_t
    {
        Unloaded = 0,
        Loading,
        Loaded,
        FadingIn,
        FadingOut,
        Unloading
    };

    using HLODProgressCallback = std::function<void(float progress, std::string_view stage)>;

} // namespace world
