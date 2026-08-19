#include <doctest.h>
#include <world/HLODCellPlanner.hpp>
#include <world/HLODTypes.hpp>
#include <world/WorldTypes.hpp>

#include <algorithm>
#include <string>
#include <vector>

// ============================================================
// VK-1594: multi-sector HLOD cell enumeration.
//
// This is the only layer of the tier 1/2 bake that is unit-testable -
// WorldSectorServiceImpl needs a SceneGraphSystem and drives EventDispatcher
// throughout, so it is never constructed by a test.
// ============================================================

namespace
{
    world::HLODTierConfig tierCfg(uint8_t tier, uint8_t cellSize)
    {
        world::HLODTierConfig cfg;
        cfg.tier = tier;
        cfg.cellSize = cellSize;
        return cfg;
    }

    std::string sectorPath(int32_t x, int32_t z)
    {
        return "worlds/sector_" + std::to_string(x) + "_" + std::to_string(z) + ".vfsector";
    }

    world::SectorFilePathMap gridOf(const std::vector<world::SectorCoord>& coords)
    {
        world::SectorFilePathMap map;
        for (const auto& c : coords)
            map[c] = sectorPath(c.x, c.z);
        return map;
    }

    const world::HLODCellPlan* findCell(const std::vector<world::HLODCellPlan>& plans,
                                        int32_t x, int32_t z, uint8_t tier)
    {
        auto it = std::find_if(plans.begin(), plans.end(), [&](const world::HLODCellPlan& p)
                               { return p.cell == world::HLODCellCoord(x, z, tier); });
        return it == plans.end() ? nullptr : &*it;
    }
}

TEST_SUITE("HLODCellPlanner")
{
    TEST_CASE("floorDivCell floors, it does not truncate")
    {
        // The whole reason this helper exists. Truncation folds -1 and 0 into cell 0.
        CHECK(world::floorDivCell(0, 2) == 0);
        CHECK(world::floorDivCell(1, 2) == 0);
        CHECK(world::floorDivCell(2, 2) == 1);
        CHECK(world::floorDivCell(-1, 2) == -1);
        CHECK(world::floorDivCell(-2, 2) == -1);
        CHECK(world::floorDivCell(-3, 2) == -2);

        CHECK(world::floorDivCell(-1, 4) == -1);
        CHECK(world::floorDivCell(-4, 4) == -1);
        CHECK(world::floorDivCell(-5, 4) == -2);
        CHECK(world::floorDivCell(3, 4) == 0);
        CHECK(world::floorDivCell(4, 4) == 1);
    }

    TEST_CASE("effectiveCellSize clamps a zero cellSize to 1")
    {
        // .vfworld stores the tier table verbatim, so a hand-edited 0 must not divide by zero.
        CHECK(world::effectiveCellSize(tierCfg(0, 0)) == 1);
        CHECK(world::effectiveCellSize(tierCfg(0, 1)) == 1);
        CHECK(world::effectiveCellSize(tierCfg(2, 4)) == 4);

        CHECK(world::sectorToCell({7, -3}, tierCfg(0, 0)) == world::HLODCellCoord(7, -3, 0));
    }

    TEST_CASE("cellSize 1 gives one cell per sector")
    {
        auto plans = world::planCellsForTier(
            gridOf({{0, 0}, {1, 0}, {0, 1}}), tierCfg(0, 1));

        REQUIRE(plans.size() == 3);
        for (const auto& p : plans)
        {
            CHECK(p.memberCoords.size() == 1);
            CHECK(p.memberSectorFiles.size() == 1);
            CHECK(p.cell.tier == 0);
            CHECK(p.cell.x == p.memberCoords[0].x);
            CHECK(p.cell.z == p.memberCoords[0].z);
        }
    }

    TEST_CASE("cellSize 2 groups a 2x2 block into one cell")
    {
        auto plans = world::planCellsForTier(
            gridOf({{0, 0}, {1, 0}, {0, 1}, {1, 1}, {2, 0}}), tierCfg(1, 2));

        REQUIRE(plans.size() == 2);

        const auto* origin = findCell(plans, 0, 0, 1);
        REQUIRE(origin != nullptr);
        CHECK(origin->memberCoords.size() == 4);
        CHECK(origin->memberSectorFiles.size() == 4);

        const auto* neighbour = findCell(plans, 1, 0, 1);
        REQUIRE(neighbour != nullptr);
        CHECK(neighbour->memberCoords.size() == 1);
        CHECK(neighbour->memberCoords[0] == world::SectorCoord(2, 0));
    }

    TEST_CASE("negative sector coords land in the correct cell")
    {
        // (-1,0) must be cell -1, not cell 0. If it folded into cell 0 the sector would be baked
        // into a cell the streamer never asks for at that position.
        auto plans = world::planCellsForTier(
            gridOf({{-1, 0}, {-2, 0}, {0, 0}, {-1, -1}}), tierCfg(1, 2));

        const auto* negative = findCell(plans, -1, 0, 1);
        REQUIRE(negative != nullptr);
        CHECK(negative->memberCoords.size() == 2); // (-2,0) and (-1,0)
        CHECK(negative->memberCoords[0] == world::SectorCoord(-2, 0));
        CHECK(negative->memberCoords[1] == world::SectorCoord(-1, 0));

        const auto* positive = findCell(plans, 0, 0, 1);
        REQUIRE(positive != nullptr);
        CHECK(positive->memberCoords.size() == 1);
        CHECK(positive->memberCoords[0] == world::SectorCoord(0, 0));

        // (-1,-1) floors on both axes
        CHECK(findCell(plans, -1, -1, 1) != nullptr);
    }

    TEST_CASE("cellSize 4 spans a 4x4 block across the origin")
    {
        auto plans = world::planCellsForTier(
            gridOf({{-4, -4}, {-1, -1}, {0, 0}, {3, 3}, {4, 4}}), tierCfg(2, 4));

        // (-4,-4) -> (-1,-1); (-1,-1) -> (-1,-1); (0,0) and (3,3) -> (0,0); (4,4) -> (1,1)
        REQUIRE(plans.size() == 3);

        const auto* negative = findCell(plans, -1, -1, 2);
        REQUIRE(negative != nullptr);
        CHECK(negative->memberCoords.size() == 2);

        const auto* origin = findCell(plans, 0, 0, 2);
        REQUIRE(origin != nullptr);
        CHECK(origin->memberCoords.size() == 2);

        CHECK(findCell(plans, 1, 1, 2) != nullptr);
    }

    TEST_CASE("sectors with no file path are skipped, never baked as empty")
    {
        world::SectorFilePathMap map = gridOf({{0, 0}, {1, 0}});
        map[world::SectorCoord(1, 1)] = ""; // registered but never saved

        auto plans = world::planCellsForTier(map, tierCfg(1, 2));

        REQUIRE(plans.size() == 1);
        CHECK(plans[0].memberCoords.size() == 2);
        for (const auto& path : plans[0].memberSectorFiles)
            CHECK_FALSE(path.empty());
    }

    TEST_CASE("a world with no saved sectors plans nothing")
    {
        CHECK(world::planCellsForTier({}, tierCfg(1, 2)).empty());

        world::SectorFilePathMap allEmpty;
        allEmpty[world::SectorCoord(0, 0)] = "";
        CHECK(world::planCellsForTier(allEmpty, tierCfg(1, 2)).empty());
    }

    TEST_CASE("output is deterministic and the parallel arrays stay in step")
    {
        // The input is an unordered_map, so without the explicit sort the bake order - and the
        // progress bar - would differ between runs.
        const auto coords = std::vector<world::SectorCoord>{
            {3, 1}, {-2, 4}, {0, 0}, {1, 1}, {-1, -1}, {5, 2}, {2, 2}};

        auto first = world::planCellsForTier(gridOf(coords), tierCfg(1, 2));
        auto second = world::planCellsForTier(gridOf(coords), tierCfg(1, 2));

        REQUIRE(first.size() == second.size());
        for (size_t i = 0; i < first.size(); ++i)
            CHECK(first[i].cell == second[i].cell);

        // Cells ascend by (z, x)
        for (size_t i = 1; i < first.size(); ++i)
        {
            const auto& prev = first[i - 1].cell;
            const auto& cur = first[i].cell;
            CHECK((prev.z < cur.z || (prev.z == cur.z && prev.x < cur.x)));
        }

        // memberSectorFiles[i] belongs to memberCoords[i] for every entry of every cell
        for (const auto& plan : first)
        {
            REQUIRE(plan.memberCoords.size() == plan.memberSectorFiles.size());
            for (size_t i = 0; i < plan.memberCoords.size(); ++i)
            {
                CHECK(plan.memberSectorFiles[i]
                      == sectorPath(plan.memberCoords[i].x, plan.memberCoords[i].z));
            }
        }
    }

    TEST_CASE("cellsContainingSector cascades a sector into all three tiers")
    {
        const auto tiers = world::HLODConfig::defaultConfig().tiers;
        REQUIRE(tiers.size() == 3);

        auto cells = world::cellsContainingSector({5, 3}, tiers);
        REQUIRE(cells.size() == 3);
        CHECK(cells[0] == world::HLODCellCoord(5, 3, 0)); // cellSize 1
        CHECK(cells[1] == world::HLODCellCoord(2, 1, 1)); // cellSize 2
        CHECK(cells[2] == world::HLODCellCoord(1, 0, 2)); // cellSize 4

        // Negative coords cascade by flooring at every tier
        auto negative = world::cellsContainingSector({-1, -5}, tiers);
        REQUIRE(negative.size() == 3);
        CHECK(negative[0] == world::HLODCellCoord(-1, -5, 0));
        CHECK(negative[1] == world::HLODCellCoord(-1, -3, 1));
        CHECK(negative[2] == world::HLODCellCoord(-1, -2, 2));
    }

    TEST_CASE("tier 0 output keeps the legacy per-sector filename")
    {
        // WorldSectorPersistenceOps rediscovers tier-0 bakes by probing exactly this string, so
        // worlds baked before VK-1594 must still resolve.
        CHECK(world::hlodOutputPathForCell({4, 2, 0}, "worlds/skirmish.vfworld",
                                           "worlds/sector_4_2.vfsector")
              == "worlds/sector_4_2_hlod0.vfHLOD");
    }

    TEST_CASE("tier 1+ output is named off the world file and carries the cell coords")
    {
        CHECK(world::hlodOutputPathForCell({2, 1, 1}, "worlds/skirmish.vfworld", "ignored")
              == "worlds/skirmish_hlod1_2_1.vfHLOD");

        CHECK(world::hlodOutputPathForCell({-3, 2, 2}, "worlds/skirmish.vfworld", "ignored")
              == "worlds/skirmish_hlod2_-3_2.vfHLOD");

        // Distinct cells never collide on a filename
        CHECK(world::hlodOutputPathForCell({1, 0, 1}, "w.vfworld", "")
              != world::hlodOutputPathForCell({0, 1, 1}, "w.vfworld", ""));
        CHECK(world::hlodOutputPathForCell({1, 1, 1}, "w.vfworld", "")
              != world::hlodOutputPathForCell({1, 1, 2}, "w.vfworld", ""));
    }

    TEST_CASE("a dot in a directory name is not mistaken for an extension")
    {
        CHECK(world::hlodOutputPathForCell({0, 0, 1}, "my.project/worlds/skirmish", "")
              == "my.project/worlds/skirmish_hlod1_0_0.vfHLOD");
    }
}
