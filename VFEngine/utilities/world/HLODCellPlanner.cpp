#include "HLODCellPlanner.hpp"

#include <algorithm>

namespace world
{
    namespace
    {
        bool sectorCoordLess(const SectorCoord& a, const SectorCoord& b)
        {
            return (a.z != b.z) ? (a.z < b.z) : (a.x < b.x);
        }

        std::string stripExtension(const std::string& path)
        {
            // rfind('.') alone would eat a dot in a parent directory name for an extensionless
            // path, so only strip a dot that comes after the last separator.
            const auto dot = path.rfind('.');
            if (dot == std::string::npos)
                return path;

            const auto slash = path.find_last_of("/\\");
            if (slash != std::string::npos && dot < slash)
                return path;

            return path.substr(0, dot);
        }
    }

    std::vector<HLODCellPlan> planCellsForTier(const SectorFilePathMap& sectorFilePaths,
                                               const HLODTierConfig& tier)
    {
        // Gather first, sort second: the input is an unordered_map, so iteration order varies
        // between runs and even between processes.
        std::unordered_map<HLODCellCoord, HLODCellPlan, HLODCellCoordHash> byCell;

        for (const auto& [coord, path] : sectorFilePaths)
        {
            if (path.empty())
                continue;

            const HLODCellCoord cell = sectorToCell(coord, tier);

            auto& plan = byCell[cell];
            plan.cell = cell;
            plan.memberCoords.push_back(coord);
            plan.memberSectorFiles.push_back(path);
        }

        std::vector<HLODCellPlan> plans;
        plans.reserve(byCell.size());
        for (auto& [cell, plan] : byCell)
        {
            // Sort the members while keeping the two parallel arrays in step. Sorting an index
            // permutation avoids zipping the vectors together and back.
            const size_t count = plan.memberCoords.size();
            std::vector<size_t> order(count);
            for (size_t i = 0; i < count; ++i)
                order[i] = i;

            std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
                      { return sectorCoordLess(plan.memberCoords[a], plan.memberCoords[b]); });

            HLODCellPlan sorted;
            sorted.cell = plan.cell;
            sorted.memberCoords.reserve(count);
            sorted.memberSectorFiles.reserve(count);
            for (size_t i : order)
            {
                sorted.memberCoords.push_back(plan.memberCoords[i]);
                sorted.memberSectorFiles.push_back(std::move(plan.memberSectorFiles[i]));
            }

            plans.push_back(std::move(sorted));
        }

        std::sort(plans.begin(), plans.end(), [](const HLODCellPlan& a, const HLODCellPlan& b)
                  { return (a.cell.z != b.cell.z) ? (a.cell.z < b.cell.z) : (a.cell.x < b.cell.x); });

        return plans;
    }

    std::vector<HLODCellCoord> cellsContainingSector(const SectorCoord& coord,
                                                     const std::vector<HLODTierConfig>& tiers)
    {
        std::vector<HLODCellCoord> cells;
        cells.reserve(tiers.size());
        for (const auto& tier : tiers)
            cells.push_back(sectorToCell(coord, tier));
        return cells;
    }

    std::string hlodOutputPathForCell(const HLODCellCoord& cell,
                                      const std::string& worldFilePath,
                                      const std::string& firstMemberSectorFilePath)
    {
        if (cell.tier == 0)
            return stripExtension(firstMemberSectorFilePath) + "_hlod0.vfHLOD";

        return stripExtension(worldFilePath)
            + "_hlod" + std::to_string(static_cast<int>(cell.tier))
            + "_" + std::to_string(cell.x)
            + "_" + std::to_string(cell.z)
            + ".vfHLOD";
    }

} // namespace world
