#pragma once

#include "WorldTypes.hpp"
#include "HLODTypes.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace world
{
    // VK-1599: one named runtime streaming grid - UE5 World Partition parity. A world declares one
    // or more; each carries its own cell size, its own load/prefetch/unload radii and its own
    // per-frame budgets, and owns the sector files bucketed into it.
    //
    // Grid 0 is always present and is the PRIMARY grid: terrain alignment, ocean tiles, navmesh
    // tile preload/release and HLOD all bind to it. Grids 1..N-1 stream entities only.
    inline constexpr const char* kDefaultGridName = "Default";

    struct GridDefinition
    {
        std::string name = kDefaultGridName;

        SectorConfig sectorConfig;
        SectorStreamingConfig streamingConfig;

        std::unordered_map<SectorCoord, std::string, SectorCoordHash> sectorFilePaths;
    };

    struct WorldDefinition
    {
        std::string name;
        std::string terrainPath;
        std::string waterDefinitionPath;

        // VK-1599: never empty. A .vfworld written before this change has no "grids" key at all -
        // its top-level sectorConfig/streamingConfig/sectors populate grids[0], which is then named
        // "Default", so an old world is simply a one-grid world and re-saves byte-identically.
        std::vector<GridDefinition> grids{GridDefinition{}};

        // HLOD is bound to the primary grid, so its config and bake inventory stay on the world
        // rather than being duplicated per grid.
        HLODConfig hlodConfig;

        // VK-1594: baked HLOD proxy path per cell, across every tier. Tier 0 cells are also
        // recorded here, but a world saved before VK-1594 has no "hlodCells" key at all - the
        // streamer then falls back to the tier-0 filename convention on WorldSector::hlodFilePath,
        // so old worlds keep resolving without a format bump.
        std::unordered_map<HLODCellCoord, std::string, HLODCellCoordHash> hlodCells;

        // ---- VK-1599 grid access ----
        //
        // Every accessor clamps rather than asserting: a gridIndex reaches here from a
        // StreamingPolicyComponent that a scene may have authored against a world with more grids
        // than this one has, and silently falling back to the primary grid is the non-destructive
        // reading of that. grids is never empty, so back() is always valid.

        [[nodiscard]] uint8_t gridCount() const noexcept
        {
            return static_cast<uint8_t>(grids.size());
        }

        [[nodiscard]] uint8_t clampGridIndex(uint8_t gridIndex) const noexcept
        {
            return gridIndex < grids.size() ? gridIndex : kPrimaryGridIndex;
        }

        [[nodiscard]] GridDefinition& grid(uint8_t gridIndex) noexcept
        {
            return grids[clampGridIndex(gridIndex)];
        }

        [[nodiscard]] const GridDefinition& grid(uint8_t gridIndex) const noexcept
        {
            return grids[clampGridIndex(gridIndex)];
        }

        [[nodiscard]] GridDefinition& primaryGrid() noexcept { return grids[kPrimaryGridIndex]; }
        [[nodiscard]] const GridDefinition& primaryGrid() const noexcept { return grids[kPrimaryGridIndex]; }
    };

} // namespace world
