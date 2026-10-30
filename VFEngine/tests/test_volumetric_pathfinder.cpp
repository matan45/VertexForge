#include <doctest.h>
#include <navigation/volumetric/VolumetricPathfinder.hpp>
#include <navigation/volumetric/VoxelGrid.hpp>

#include <string>
#include <vector>

// ============================================================
// VK-1660: VoxelGrid neighbours (no corner cutting) and the volumetric A* (volumetric::findPath): smoothing,
// detours, the node cap, snapping blocked ends and every failure status.
// ============================================================

namespace
{
    namespace vol = volumetric;

    vol::VoxelGrid openGrid(const glm::ivec3& dims, uint8_t connectivity = 26, float voxelSize = 1.0f)
    {
        vol::VolumetricBakeSettings settings;
        settings.voxelSize = voxelSize;
        settings.connectivity = connectivity;
        vol::VoxelGrid grid(glm::vec3(0.0f), dims, settings);
        for (auto& cell : grid.data())
            cell = 1;
        return grid;
    }

    void block(vol::VoxelGrid& grid, int x, int y, int z)
    {
        grid.setNavigable(vol::VoxelCoord{x, y, z}, false);
    }

    vol::PathRequest request(const glm::vec3& start, const glm::vec3& end, float snapRadius = 2.0f)
    {
        vol::PathRequest r;
        r.start = start;
        r.end = end;
        r.snapRadius = snapRadius;
        return r;
    }

    bool segmentsClear(const vol::VoxelGrid& grid, const std::vector<glm::vec3>& points)
    {
        for (size_t i = 1; i < points.size(); ++i)
        {
            if (!vol::hasLineOfSight(grid, points[i - 1], points[i]))
                return false;
        }
        return true;
    }
}

TEST_SUITE("VolumetricPathfinder")
{
    TEST_CASE("neighbours follow the connectivity and never cut a blocked corner")
    {
        vol::VoxelGrid six = openGrid(glm::ivec3(3), 6);
        std::vector<vol::VoxelCoord> out;
        six.neighbors(vol::VoxelCoord{1, 1, 1}, out);
        CHECK(out.size() == 6);

        vol::VoxelGrid all = openGrid(glm::ivec3(3), 26);
        all.neighbors(vol::VoxelCoord{1, 1, 1}, out);
        CHECK(out.size() == 26);

        // Blocking one face neighbour removes it, the 4 edge steps and the 4 corner steps that pass it.
        block(all, 2, 1, 1);
        all.neighbors(vol::VoxelCoord{1, 1, 1}, out);
        CHECK(out.size() == 26 - 1 - 4 - 4);
    }

    TEST_CASE("a straight path through open space is one segment")
    {
        const vol::VoxelGrid grid = openGrid(glm::ivec3(10, 3, 3));
        const auto result = vol::findPath(grid, request({0.5f, 1.5f, 1.5f}, {9.5f, 1.5f, 1.5f}));
        REQUIRE(result.status == vol::VolumePathStatus::Found);
        REQUIRE(result.waypoints.size() == 2);
        CHECK(result.waypoints.front() == glm::vec3(0.5f, 1.5f, 1.5f));
        CHECK(result.waypoints.back() == glm::vec3(9.5f, 1.5f, 1.5f));
        CHECK(result.nodesExpanded > 0);
        CHECK_FALSE(result.startSnapped);
        CHECK_FALSE(result.endSnapped);
    }

    TEST_CASE("the path goes around a wall and every leg is clear")
    {
        vol::VoxelGrid grid = openGrid(glm::ivec3(10, 1, 10));
        for (int z = 0; z <= 7; ++z)
            block(grid, 5, 0, z);

        const auto result = vol::findPath(grid, request({1.5f, 0.5f, 1.5f}, {8.5f, 0.5f, 1.5f}));
        REQUIRE(result.status == vol::VolumePathStatus::Found);
        REQUIRE(result.waypoints.size() >= 3);
        CHECK(segmentsClear(grid, result.waypoints));
        bool passesTheGap = false;
        for (const glm::vec3& point : result.waypoints)
            passesTheGap = passesTheGap || point.z > 8.0f;
        CHECK(passesTheGap);
    }

    TEST_CASE("a diagonal crack between two blocked voxels is not a way through")
    {
        vol::VoxelGrid grid = openGrid(glm::ivec3(2, 1, 2));
        block(grid, 1, 0, 0);
        block(grid, 0, 0, 1);

        CHECK_FALSE(vol::hasLineOfSight(grid, {0.5f, 0.5f, 0.5f}, {1.5f, 0.5f, 1.5f}));
        const auto result = vol::findPath(grid, request({0.5f, 0.5f, 0.5f}, {1.5f, 0.5f, 1.5f}));
        CHECK(result.status == vol::VolumePathStatus::NoPath);
        CHECK(result.waypoints.empty());
    }

    TEST_CASE("the node cap returns a partial path toward the end")
    {
        const vol::VoxelGrid grid = openGrid(glm::ivec3(64, 1, 1));
        auto r = request({0.5f, 0.5f, 0.5f}, {63.5f, 0.5f, 0.5f});
        r.maxNodes = 10;
        const auto result = vol::findPath(grid, r);
        REQUIRE(result.status == vol::VolumePathStatus::Partial);
        CHECK(result.nodesExpanded == 10);
        REQUIRE_FALSE(result.waypoints.empty());
        CHECK(result.waypoints.front() == glm::vec3(0.5f, 0.5f, 0.5f));
        CHECK(result.waypoints.back().x == doctest::Approx(9.5f)); // the expanded voxel nearest the end
    }

    TEST_CASE("an unreachable end gives the partial path to the nearest reached voxel")
    {
        vol::VoxelGrid grid = openGrid(glm::ivec3(8, 1, 1));
        block(grid, 4, 0, 0);
        const auto result = vol::findPath(grid, request({0.5f, 0.5f, 0.5f}, {7.5f, 0.5f, 0.5f}));
        REQUIRE(result.status == vol::VolumePathStatus::Partial);
        CHECK(result.waypoints.back().x == doctest::Approx(3.5f));
    }

    TEST_CASE("a blocked start or end snaps to the nearest navigable voxel within the radius")
    {
        vol::VoxelGrid grid = openGrid(glm::ivec3(5, 1, 1));
        block(grid, 0, 0, 0);
        block(grid, 4, 0, 0);

        const auto snapped = vol::findPath(grid, request({0.5f, 0.5f, 0.5f}, {4.5f, 0.5f, 0.5f}, 2.0f));
        REQUIRE(snapped.status == vol::VolumePathStatus::Found);
        REQUIRE(snapped.startSnapped);
        CHECK(*snapped.startSnapped == glm::vec3(1.5f, 0.5f, 0.5f));
        REQUIRE(snapped.endSnapped);
        CHECK(*snapped.endSnapped == glm::vec3(3.5f, 0.5f, 0.5f));
        CHECK(snapped.waypoints.front() == glm::vec3(1.5f, 0.5f, 0.5f));
        CHECK(snapped.waypoints.back() == glm::vec3(3.5f, 0.5f, 0.5f));

        CHECK(vol::findPath(grid, request({0.5f, 0.5f, 0.5f}, {2.5f, 0.5f, 0.5f}, 0.5f)).status ==
              vol::VolumePathStatus::StartBlocked);
        CHECK(vol::findPath(grid, request({2.5f, 0.5f, 0.5f}, {4.5f, 0.5f, 0.5f}, 0.5f)).status ==
              vol::VolumePathStatus::EndBlocked);
    }

    TEST_CASE("snapping picks the voxel nearest to the point")
    {
        vol::VoxelGrid grid = openGrid(glm::ivec3(3, 1, 1));
        block(grid, 1, 0, 0);
        // In the blocked middle voxel, nearer its +X side: the +X neighbour wins.
        const auto nearest = vol::snapToNavigable(grid, {1.8f, 0.5f, 0.5f}, 2.0f);
        REQUIRE(nearest);
        CHECK(*nearest == glm::vec3(2.5f, 0.5f, 0.5f));
        // A navigable point is returned as it is; a point outside the grid gets nothing.
        CHECK(*vol::snapToNavigable(grid, {0.25f, 0.5f, 0.5f}, 2.0f) == glm::vec3(0.25f, 0.5f, 0.5f));
        CHECK_FALSE(vol::snapToNavigable(grid, {-1.0f, 0.5f, 0.5f}, 2.0f));
    }

    TEST_CASE("ends outside the grid and ends in one voxel")
    {
        const vol::VoxelGrid grid = openGrid(glm::ivec3(4));
        CHECK(vol::findPath(grid, request({-1.0f, 1.0f, 1.0f}, {2.0f, 2.0f, 2.0f})).status ==
              vol::VolumePathStatus::OutsideVolume);
        CHECK(vol::findPath(grid, request({1.0f, 1.0f, 1.0f}, {2.0f, 2.0f, 9.0f})).status ==
              vol::VolumePathStatus::OutsideVolume);

        const auto same = vol::findPath(grid, request({1.2f, 1.2f, 1.2f}, {1.8f, 1.7f, 1.6f}));
        REQUIRE(same.status == vol::VolumePathStatus::Found);
        REQUIRE(same.waypoints.size() == 2);
        CHECK(same.waypoints.front() == glm::vec3(1.2f, 1.2f, 1.2f));
        CHECK(same.waypoints.back() == glm::vec3(1.8f, 1.7f, 1.6f));
    }

    TEST_CASE("status names are canonical")
    {
        CHECK(std::string(vol::volumePathStatusName(vol::VolumePathStatus::Found)) == "Found");
        CHECK(std::string(vol::volumePathStatusName(vol::VolumePathStatus::DifferentVolumes)) == "DifferentVolumes");
        CHECK(std::string(vol::volumePathStatusName(vol::VolumePathStatus::NoPath)) == "NoPath");
    }
}
