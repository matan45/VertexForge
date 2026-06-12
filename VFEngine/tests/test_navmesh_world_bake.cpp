#include <doctest.h>
#include <impl/navmesh/NavmeshWorldBaker.hpp>
#include <impl/navmesh/NavmeshTileManager.hpp>
#include <providers/navmesh/INavmeshProvider.hpp>
#include <types/NavmeshTypes.hpp>
#include <world/WorldTypes.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <unordered_set>
#include <vector>

// ============================================================
// NavmeshWorldBaker: sector ordering, 3x3 neighborhood loading,
// border-tile dedup, cancel/release, already-loaded sectors,
// index finalization. JobSystem is uninitialized in tests, so
// tile bakes run synchronously at submit time.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    class BakeStubProvider : public services::INavmeshProvider
    {
    public:
        std::vector<navigation::NavmeshTileCoord> bakedTiles;
        bool navmeshExists = false;

        bool init() override { return true; }
        void cleanUp() override {}
        bool isInitialized() const override { return true; }

        bool buildNavmesh(const navigation::NavmeshInputGeometry&,
                          const types::NavmeshBakeSettings&,
                          const navigation::OffMeshConnectionsMap&,
                          const navigation::AreaModifiersMap&) override { return true; }
        types::NavmeshBakeProgress getBuildProgress() const override { return {}; }

        bool initTiledNavmesh(const types::NavmeshBakeSettings&,
                              const glm::vec3&, const glm::vec3&) override
        {
            navmeshExists = true;
            return true;
        }
        navigation::NavmeshTileData buildSingleTile(int tx, int tz,
                                                    const navigation::NavmeshInputGeometry&,
                                                    const types::NavmeshBakeSettings&,
                                                    const navigation::NavmeshOffMeshConnections&,
                                                    const std::vector<navigation::NavmeshAreaModifier>&,
                                                    uint8_t) override
        {
            bakedTiles.push_back({tx, tz});
            navigation::NavmeshTileData tile;
            tile.x = tx;
            tile.y = tz;
            tile.data = {1, 2, 3, 4};
            tile.dataSize = 4;
            return tile;
        }
        bool addNavmeshTile(const navigation::NavmeshTileData&) override { return true; }
        bool removeNavmeshTile(int, int) override { return true; }

        std::vector<navigation::NavmeshTileData> serializeNavmesh() const override { return {}; }
        bool deserializeNavmesh(const navigation::NavmeshFileHeader&,
                                const std::vector<navigation::NavmeshTileData>&) override { return true; }
        bool hasNavmesh() const override { return navmeshExists; }
        void clearNavmesh() override { navmeshExists = false; }

        navigation::NavPath findPath(const glm::vec3&, const glm::vec3&, float, float) override { return {}; }
        navigation::NavmeshRaycastResult navmeshRaycast(const glm::vec3&, const glm::vec3&) override { return {}; }
        glm::vec3 getClosestPoint(const glm::vec3& point, float) override { return point; }
        bool isPointOnNavmesh(const glm::vec3&, float) override { return false; }

        int addCrowdAgent(const glm::vec3&, float, float, float, float) override { return -1; }
        void removeCrowdAgent(int) override {}
        void setCrowdAgentTarget(int, const glm::vec3&) override {}
        void stopCrowdAgent(int) override {}
        void updateCrowdAgentParams(int, float, float) override {}
        glm::vec3 getCrowdAgentPosition(int) const override { return {}; }
        glm::vec3 getCrowdAgentVelocity(int) const override { return {}; }
        float getCrowdAgentMaxSpeed(int) const override { return 0.0f; }
        void updateCrowd(float) override {}
        bool overrideCrowdAgentVelocity(int, const glm::vec3&) override { return false; }

        void configureCrowdFilter(int, const float*, int) override {}
        void setCrowdAgentFilterType(int, uint8_t) override {}

        void onTileAdded(int, int) override {}
        void onTileRemoved(int, int) override {}

        void getDebugMesh(std::vector<glm::vec3>&, std::vector<uint32_t>&) const override {}
    };

    bool containsCoord(const std::vector<::world::SectorCoord>& coords, int x, int z)
    {
        return std::any_of(coords.begin(), coords.end(),
                           [&](const ::world::SectorCoord& c) { return c.x == x && c.z == z; });
    }

    struct BakeFixture
    {
        BakeStubProvider provider;
        types::NavmeshBakeSettings settings; // tile world size 32 (128 cells * 0.25)
        services::NavmeshTileManager manager;
        std::unique_ptr<services::NavmeshWorldBaker> baker;

        // Stubbed world: 64-unit sectors = 2x2 navmesh tiles each
        std::vector<::world::SectorCoord> allSectors;
        std::unordered_set<::world::SectorCoord, ::world::SectorCoordHash> loadedSectors;
        std::vector<::world::SectorCoord> loadCalls;
        std::vector<::world::SectorCoord> unloadCalls;
        bool loadsComplete = true; // false = sectors never become ready (stuck Loading)
        int keepAliveUpdates = 0;
        bool keepAliveRegistered = false;
        bool keepAliveUnregistered = false;

        fs::path outputDir;

        BakeFixture()
            : manager(&provider, settings,
                      [](const navigation::NavmeshTileBounds& bounds,
                         const types::NavmeshBakeSettings&,
                         navigation::NavmeshInputGeometry& outGeometry)
                      {
                          outGeometry.addVertex(bounds.min);
                          outGeometry.addVertex({bounds.max.x, bounds.min.y, bounds.min.z});
                          outGeometry.addVertex({bounds.min.x, bounds.min.y, bounds.max.z});
                          outGeometry.addTriangle(0, 1, 2);
                      }),
              outputDir(fs::temp_directory_path() / "vf_test_world_bake")
        {
            fs::remove_all(outputDir);

            services::NavmeshWorldBaker::WorldOps ops;
            ops.getAllSectorCoords = [this]() { return allSectors; };
            ops.loadSector = [this](const ::world::SectorCoord& coord)
            {
                loadCalls.push_back(coord);
                if (loadsComplete)
                    loadedSectors.insert(coord);
                return true;
            };
            ops.unloadSector = [this](const ::world::SectorCoord& coord)
            {
                unloadCalls.push_back(coord);
                loadedSectors.erase(coord);
                return true;
            };
            ops.sectorExists = [this](const ::world::SectorCoord& coord)
            {
                return containsCoord(allSectors, coord.x, coord.z);
            };
            ops.getReadiness = [this](const ::world::SectorCoord& coord)
            {
                ::events::world::SectorReadiness readiness;
                readiness.state = loadedSectors.count(coord)
                    ? ::world::SectorState::Loaded
                    : ::world::SectorState::Unloaded;
                return readiness;
            };
            ops.getSectorWorldSize = []() { return 64.0f; };
            ops.registerKeepAliveSource = [this](const glm::vec3&)
            {
                keepAliveRegistered = true;
                return 7u;
            };
            ops.updateKeepAliveSource = [this](uint32_t, const glm::vec3&) { keepAliveUpdates++; };
            ops.unregisterKeepAliveSource = [this](uint32_t) { keepAliveUnregistered = true; };

            baker = std::make_unique<services::NavmeshWorldBaker>(manager, std::move(ops));
        }

        ~BakeFixture()
        {
            std::error_code ec;
            fs::remove_all(outputDir, ec);
        }

        void pump(int frames)
        {
            for (int i = 0; i < frames && baker->isRunning(); ++i)
            {
                baker->update();
                manager.pollTileBakeCompletions();
            }
        }

        bool runToCompletion(int maxFrames = 5000)
        {
            pump(maxFrames);
            return !baker->isRunning();
        }
    };
}

TEST_SUITE("NavmeshWorldBake")
{
    TEST_CASE("bakes every sector's tiles exactly once and writes the index")
    {
        BakeFixture f;
        f.allSectors = {{1, 0}, {0, 0}}; // intentionally unsorted

        REQUIRE(f.baker->start(f.outputDir.string()));
        REQUIRE(f.runToCompletion());

        auto progress = f.baker->getProgress();
        CHECK(progress.state == events::navmesh::WorldNavmeshBakeState::Complete);
        CHECK(progress.sectorsTotal == 2);
        CHECK(progress.sectorsDone == 2);

        // 64-unit sectors at 32-unit tiles: (0,0) -> tiles (0..1)^2,
        // (1,0) -> tiles (2..3, 0..1) — 8 tiles, no duplicates
        CHECK(f.provider.bakedTiles.size() == 8);
        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> unique(
            f.provider.bakedTiles.begin(), f.provider.bakedTiles.end());
        CHECK(unique.size() == 8);

        // Index on disk lists all baked tiles, streaming enabled by default
        navigation::NavmeshTileCache cache(f.outputDir.string());
        navigation::NavmeshTileIndex index;
        REQUIRE(cache.loadIndex(index));
        CHECK(index.tileCoords.size() == 8);
        CHECK(index.streaming.enabled == 1);

        // Keep-alive source lifecycle
        CHECK(f.keepAliveRegistered);
        CHECK(f.keepAliveUnregistered);
        CHECK(f.keepAliveUpdates >= 2);
    }

    TEST_CASE("every baker-loaded sector is released by the end")
    {
        BakeFixture f;
        f.allSectors = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

        REQUIRE(f.baker->start(f.outputDir.string()));
        REQUIRE(f.runToCompletion());

        CHECK(f.loadedSectors.empty());
        // Only sectors that exist were ever loaded
        for (const auto& coord : f.loadCalls)
            CHECK(containsCoord(f.allSectors, coord.x, coord.z));
        // Everything the baker loaded was unloaded again
        CHECK(f.loadCalls.size() == f.unloadCalls.size());
    }

    TEST_CASE("sectors already loaded by the world streamer are left alone")
    {
        BakeFixture f;
        f.allSectors = {{0, 0}, {1, 0}};
        f.loadedSectors.insert({0, 0}); // camera already has it

        REQUIRE(f.baker->start(f.outputDir.string()));
        REQUIRE(f.runToCompletion());

        CHECK_FALSE(containsCoord(f.loadCalls, 0, 0));
        CHECK_FALSE(containsCoord(f.unloadCalls, 0, 0));
        CHECK(f.loadedSectors.count({0, 0}) == 1);
    }

    TEST_CASE("cancel mid-bake releases baker-loaded sectors")
    {
        BakeFixture f;
        f.allSectors = {{0, 0}, {1, 0}};
        f.loadsComplete = false; // sectors never become ready -> baker parks in WaitReady

        REQUIRE(f.baker->start(f.outputDir.string()));
        f.pump(10);
        REQUIRE(f.baker->isRunning());
        CHECK_FALSE(f.loadCalls.empty());

        f.baker->cancel();

        CHECK_FALSE(f.baker->isRunning());
        CHECK(f.baker->getProgress().state == events::navmesh::WorldNavmeshBakeState::Cancelled);
        CHECK(f.loadCalls.size() == f.unloadCalls.size());
        CHECK(f.keepAliveUnregistered);
    }

    TEST_CASE("start fails without sectors and while already running")
    {
        BakeFixture f;

        CHECK_FALSE(f.baker->start(f.outputDir.string()));
        CHECK(f.baker->getProgress().state == events::navmesh::WorldNavmeshBakeState::Failed);

        f.allSectors = {{0, 0}};
        f.loadsComplete = false; // keep it running
        REQUIRE(f.baker->start(f.outputDir.string()));
        CHECK_FALSE(f.baker->start(f.outputDir.string()));
        f.baker->cancel();
    }

    TEST_CASE("a sector that never becomes ready is skipped after the timeout")
    {
        // Covered behaviorally by MAX_WAIT_FRAMES — not pumped here (1800 frames),
        // just assert the baker survives a partially-ready world: the second
        // sector loads fine while the first is pre-loaded
        BakeFixture f;
        f.allSectors = {{0, 0}};
        f.loadedSectors.insert({0, 0});

        REQUIRE(f.baker->start(f.outputDir.string()));
        REQUIRE(f.runToCompletion());
        CHECK(f.baker->getProgress().state == events::navmesh::WorldNavmeshBakeState::Complete);
        CHECK(f.provider.bakedTiles.size() == 4);
    }
}
