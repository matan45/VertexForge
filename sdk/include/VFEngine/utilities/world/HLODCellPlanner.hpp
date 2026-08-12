#pragma once

#include "WorldExport.hpp"
#include "HLODTypes.hpp"
#include "WorldTypes.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace world
{
    // VK-1594: turns the world's flat sector table into the per-tier cell work list the HLOD bake
    // consumes. Deliberately pure - no file IO, no singletons, no engine state - because
    // WorldSectorServiceImpl is not unit-constructible, so this is the only layer of the multi-tier
    // bake that can be covered by CPU tests.
#pragma warning(push)
#pragma warning(disable: 4251)
    struct HLODCellPlan
    {
        HLODCellCoord cell;

        // Parallel arrays, both sorted by (z, x): memberSectorFiles[i] is the .vfsector path of
        // memberCoords[i]. Sectors with no file on the world definition are skipped entirely, so a
        // plan never carries an empty path.
        std::vector<SectorCoord> memberCoords;
        std::vector<std::string> memberSectorFiles;
    };
#pragma warning(pop)

    using SectorFilePathMap = std::unordered_map<SectorCoord, std::string, SectorCoordHash>;

    // Every cell of `tier` that contains at least one sector with a file path, sorted by (z, x) so
    // bake order - and therefore test expectations and progress reporting - are deterministic
    // despite the unordered_map input.
    [[nodiscard]] VF_WORLD_API std::vector<HLODCellPlan> planCellsForTier(
        const SectorFilePathMap& sectorFilePaths,
        const HLODTierConfig& tier);

    // The cells across every configured tier that contain `coord`. Used by HLOD invalidation to
    // cascade a dirty sector into its tier 1/2 parents.
    [[nodiscard]] VF_WORLD_API std::vector<HLODCellCoord> cellsContainingSector(
        const SectorCoord& coord,
        const std::vector<HLODTierConfig>& tiers);

    // Bake output path for a cell.
    //
    // Tier 0 keeps the historical per-sector naming - "<sector file sans extension>_hlod0.vfHLOD" -
    // because WorldSectorPersistenceOps rediscovers tier-0 bakes by probing exactly that string on
    // world load, and worlds baked before VK-1594 must keep resolving.
    //
    // Tiers 1+ are not per-sector, so they are named off the world file:
    // "<world file sans extension>_hlod<tier>_<cx>_<cz>.vfHLOD".
    [[nodiscard]] VF_WORLD_API std::string hlodOutputPathForCell(
        const HLODCellCoord& cell,
        const std::string& worldFilePath,
        const std::string& firstMemberSectorFilePath);

} // namespace world
