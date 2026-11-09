#include <doctest.h>
#include <navigation/volumetric/VolumetricNavWorld.hpp>

#include <memory>
#include <vector>

// ============================================================
// VK-1660: VolumetricNavWorld - the loaded volumes keyed by entity UUID: the smallest containing volume, the
// cross-volume path statuses, replacing and removing volumes, point queries.
// ============================================================

namespace
{
    namespace vol = volumetric;

    std::shared_ptr<const vol::VoxelGrid> openVolume(const glm::vec3& origin, const glm::ivec3& dims)
    {
        auto grid = std::make_shared<vol::VoxelGrid>(origin, dims, vol::VolumetricBakeSettings{});
        for (auto& cell : grid->data())
            cell = 1;
        return grid;
    }

    vol::PathRequest request(const glm::vec3& start, const glm::vec3& end)
    {
        vol::PathRequest r;
        r.start = start;
        r.end = end;
        return r;
    }
}

TEST_SUITE("VolumetricNavWorld")
{
    TEST_CASE("the smallest volume containing a point wins; ties go to the lowest id")
    {
        vol::VolumetricNavWorld world;
        world.setVolume(7, openVolume(glm::vec3(0.0f), glm::ivec3(20)));
        world.setVolume(9, openVolume(glm::vec3(2.0f), glm::ivec3(4)));
        world.setVolume(3, openVolume(glm::vec3(2.0f), glm::ivec3(4))); // same size as 9

        CHECK(world.volumeAt(glm::vec3(3.0f)) == 3);
        CHECK(world.volumeAt(glm::vec3(15.0f)) == 7);
        CHECK(world.volumeAt(glm::vec3(-5.0f)) == 0);
        CHECK(world.volumeIds() == std::vector<uint64_t>{3, 7, 9});
    }

    TEST_CASE("path statuses across volumes")
    {
        vol::VolumetricNavWorld world;
        CHECK(world.findPath(request(glm::vec3(1.0f), glm::vec3(2.0f))).status == vol::VolumePathStatus::NoVolume);

        world.setVolume(1, openVolume(glm::vec3(0.0f), glm::ivec3(4)));
        world.setVolume(2, openVolume(glm::vec3(10.0f), glm::ivec3(4)));

        CHECK(world.findPath(request(glm::vec3(1.0f), glm::vec3(50.0f))).status == vol::VolumePathStatus::OutsideVolume);
        CHECK(world.findPath(request(glm::vec3(-3.0f), glm::vec3(1.0f))).status == vol::VolumePathStatus::OutsideVolume);
        CHECK(world.findPath(request(glm::vec3(1.0f), glm::vec3(11.0f))).status == vol::VolumePathStatus::DifferentVolumes);

        const auto inside = world.findPath(request(glm::vec3(10.5f), glm::vec3(13.5f)));
        CHECK(inside.status == vol::VolumePathStatus::Found);
        CHECK(inside.volumeId == 2);
    }

    TEST_CASE("the path uses the smallest volume that holds both ends")
    {
        vol::VolumetricNavWorld world;
        world.setVolume(1, openVolume(glm::vec3(0.0f), glm::ivec3(30)));
        world.setVolume(2, openVolume(glm::vec3(0.0f), glm::ivec3(6)));
        CHECK(world.findPath(request(glm::vec3(1.0f), glm::vec3(5.0f))).volumeId == 2);
        CHECK(world.findPath(request(glm::vec3(1.0f), glm::vec3(20.0f))).volumeId == 1);
    }

    TEST_CASE("volumes are replaced, removed and cleared")
    {
        vol::VolumetricNavWorld world;
        auto first = openVolume(glm::vec3(0.0f), glm::ivec3(4));
        auto second = openVolume(glm::vec3(0.0f), glm::ivec3(8));
        world.setVolume(5, first);
        CHECK(world.getVolume(5).get() == first.get());
        world.setVolume(5, second);
        CHECK(world.getVolume(5).get() == second.get());

        world.setVolume(5, nullptr); // a null grid removes the volume
        CHECK(world.getVolume(5).get() == nullptr);

        world.setVolume(0, first); // 0 is "no volume": ignored
        CHECK(world.volumeIds().empty());

        world.setVolume(6, first);
        world.setVolume(8, second);
        world.removeVolume(6);
        CHECK(world.volumeIds() == std::vector<uint64_t>{8});
        world.clear();
        CHECK(world.volumeIds().empty());
    }

    TEST_CASE("point queries report the volume, navigability and the nearest navigable point")
    {
        auto grid = std::make_shared<vol::VoxelGrid>(glm::vec3(0.0f), glm::ivec3(3, 1, 1), vol::VolumetricBakeSettings{});
        grid->setNavigable(vol::VoxelCoord{0, 0, 0}, true);
        grid->setNavigable(vol::VoxelCoord{2, 0, 0}, true);
        vol::VolumetricNavWorld world;
        world.setVolume(4, grid);

        const auto blocked = world.queryPoint(glm::vec3(1.8f, 0.5f, 0.5f), 2.0f);
        CHECK(blocked.volumeId == 4);
        CHECK_FALSE(blocked.navigable);
        REQUIRE(blocked.nearest);
        CHECK(*blocked.nearest == glm::vec3(2.5f, 0.5f, 0.5f));

        const auto open = world.queryPoint(glm::vec3(0.4f, 0.5f, 0.5f), 2.0f);
        CHECK(open.navigable);
        REQUIRE(open.nearest);
        CHECK(*open.nearest == glm::vec3(0.4f, 0.5f, 0.5f));

        const auto outside = world.queryPoint(glm::vec3(9.0f), 2.0f);
        CHECK(outside.volumeId == 0);
        CHECK_FALSE(outside.navigable);
        CHECK_FALSE(outside.nearest);
    }
}
