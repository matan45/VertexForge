#include <doctest.h>
#include <impl/navmesh/NavmeshTileManager.hpp>
#include <providers/navmesh/INavmeshProvider.hpp>
#include <types/NavmeshTypes.hpp>

#include <algorithm>
#include <vector>

// ============================================================
// Navmesh <-> World Sector streaming coordination:
// tile-range math, per-sector ref counting, invoker protection
// ============================================================

namespace
{
    // Records tile add/remove calls; everything else is a no-op
    class StubNavmeshProvider : public services::INavmeshProvider
    {
    public:
        std::vector<navigation::NavmeshTileCoord> removedTiles;
        std::vector<navigation::NavmeshTileCoord> addedTiles;

        bool init() override { return true; }
        void cleanUp() override {}
        bool isInitialized() const override { return true; }

        bool buildNavmesh(const navigation::NavmeshInputGeometry&,
                          const types::NavmeshBakeSettings&,
                          const navigation::OffMeshConnectionsMap&,
                          const navigation::AreaModifiersMap&) override { return true; }
        types::NavmeshBakeProgress getBuildProgress() const override { return {}; }

        bool initTiledNavmesh(const types::NavmeshBakeSettings&,
                              const glm::vec3&, const glm::vec3&) override { return true; }
        navigation::NavmeshTileData buildSingleTile(int, int,
                                                    const navigation::NavmeshInputGeometry&,
                                                    const types::NavmeshBakeSettings&,
                                                    const navigation::NavmeshOffMeshConnections&,
                                                    const std::vector<navigation::NavmeshAreaModifier>&,
                                                    uint8_t) override { return {}; }
        bool addNavmeshTile(const navigation::NavmeshTileData& tileData) override
        {
            addedTiles.push_back({tileData.x, tileData.y});
            return true;
        }
        bool removeNavmeshTile(int tx, int tz) override
        {
            removedTiles.push_back({tx, tz});
            return true;
        }

        std::vector<navigation::NavmeshTileData> serializeNavmesh() const override { return {}; }
        bool deserializeNavmesh(const navigation::NavmeshFileHeader&,
                                const std::vector<navigation::NavmeshTileData>&) override { return true; }
        bool hasNavmesh() const override { return true; }
        void clearNavmesh() override {}

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

        bool wasRemoved(int tx, int tz) const
        {
            return std::any_of(removedTiles.begin(), removedTiles.end(),
                               [&](const navigation::NavmeshTileCoord& c)
                               { return c.x == tx && c.z == tz; });
        }
    };

    struct Fixture
    {
        StubNavmeshProvider provider;
        types::NavmeshBakeSettings settings; // must outlive the manager (held by reference)
        services::NavmeshTileManager manager;

        // tileSize 128 cells * cellSize 0.25 = tile world size 32
        Fixture()
            : manager(&provider, settings,
                      [](const navigation::NavmeshTileBounds&,
                         const types::NavmeshBakeSettings&,
                         navigation::NavmeshInputGeometry&) {})
        {
        }
    };

    glm::vec3 sectorMin(float x, float z) { return {x, -1000.0f, z}; }
    glm::vec3 sectorMax(float x, float z) { return {x, 1000.0f, z}; }
}

TEST_SUITE("NavmeshSectorStreaming")
{
    TEST_CASE("computeTilesForBounds covers the sector range without bleeding over")
    {
        Fixture f;

        SUBCASE("128-unit sector = exactly 4x4 navmesh tiles")
        {
            auto tiles = f.manager.computeTilesForBounds(sectorMin(0.0f, 0.0f),
                                                         sectorMax(128.0f, 128.0f));
            CHECK(tiles.size() == 16);
            // The max edge (exactly 128) must not include tile row/column 4
            for (const auto& t : tiles)
            {
                CHECK(t.x >= 0);
                CHECK(t.x <= 3);
                CHECK(t.z >= 0);
                CHECK(t.z <= 3);
            }
        }

        SUBCASE("negative-coordinate sector")
        {
            auto tiles = f.manager.computeTilesForBounds(sectorMin(-64.0f, -64.0f),
                                                         sectorMax(0.0f, 0.0f));
            CHECK(tiles.size() == 4);
            for (const auto& t : tiles)
            {
                CHECK(t.x >= -2);
                CHECK(t.x <= -1);
                CHECK(t.z >= -2);
                CHECK(t.z <= -1);
            }
        }
    }

    TEST_CASE("worldToTileCoord floors correctly across the origin")
    {
        Fixture f;
        CHECK(f.manager.worldToTileCoord({5.0f, 0.0f, 5.0f}).x == 0);
        CHECK(f.manager.worldToTileCoord({33.0f, 0.0f, 0.0f}).x == 1);
        CHECK(f.manager.worldToTileCoord({-5.0f, 0.0f, -5.0f}).x == -1);
        CHECK(f.manager.worldToTileCoord({-5.0f, 0.0f, -5.0f}).z == -1);
    }

    TEST_CASE("shared tiles survive until the last referencing sector releases")
    {
        Fixture f;

        // Sector A covers tiles (0..1)^2, sector B covers tiles (1..2)^2 — tile
        // (1,1) is shared. Mark everything as loaded so releases attempt unloads.
        for (int x = 0; x <= 2; ++x)
            for (int z = 0; z <= 2; ++z)
                f.manager.getStreamer().markTileLoaded({x, z});

        world::SectorCoord sectorA{0, 0};
        world::SectorCoord sectorB{1, 1};
        f.manager.prioritizeTilesForBounds(sectorA, sectorMin(0.0f, 0.0f), sectorMax(64.0f, 64.0f));
        f.manager.prioritizeTilesForBounds(sectorB, sectorMin(32.0f, 32.0f), sectorMax(96.0f, 96.0f));

        f.manager.releaseTilesForSector(sectorA, sectorMin(0.0f, 0.0f), sectorMax(64.0f, 64.0f));

        // Tiles unique to A are gone; the shared tile is still referenced by B
        CHECK(f.provider.wasRemoved(0, 0));
        CHECK(f.provider.wasRemoved(0, 1));
        CHECK(f.provider.wasRemoved(1, 0));
        CHECK_FALSE(f.provider.wasRemoved(1, 1));
        CHECK(f.manager.getStreamer().isTileLoaded({1, 1}));
        CHECK_FALSE(f.manager.getStreamer().isTileLoaded({0, 0}));

        f.manager.releaseTilesForSector(sectorB, sectorMin(32.0f, 32.0f), sectorMax(96.0f, 96.0f));
        CHECK(f.provider.wasRemoved(1, 1));
        CHECK(f.provider.wasRemoved(2, 2));
        CHECK_FALSE(f.manager.getStreamer().isTileLoaded({1, 1}));
    }

    TEST_CASE("tiles inside an invoker's unload radius survive sector release")
    {
        Fixture f;

        f.manager.getStreamer().markTileLoaded({0, 0});
        f.manager.getStreamer().markTileLoaded({1, 0});

        world::SectorCoord sector{0, 0};
        f.manager.prioritizeTilesForBounds(sector, sectorMin(0.0f, 0.0f), sectorMax(64.0f, 32.0f));

        // Invoker (e.g. an AI agent) parked on tile (0,0); radius covers ~one tile
        services::StreamingSource invoker;
        invoker.position = glm::vec3(16.0f, 0.0f, 16.0f);
        invoker.unloadRadiusSq = 24.0f * 24.0f;
        f.manager.setInvokerSources({invoker});

        f.manager.releaseTilesForSector(sector, sectorMin(0.0f, 0.0f), sectorMax(64.0f, 32.0f));

        CHECK_FALSE(f.provider.wasRemoved(0, 0)); // protected by the invoker
        CHECK(f.provider.wasRemoved(1, 0));       // outside its radius
        CHECK(f.manager.getStreamer().isTileLoaded({0, 0}));
    }

    TEST_CASE("releasing a sector that was never prioritized is a no-op")
    {
        Fixture f;
        f.manager.getStreamer().markTileLoaded({0, 0});

        f.manager.releaseTilesForSector({0, 0}, sectorMin(0.0f, 0.0f), sectorMax(32.0f, 32.0f));

        CHECK(f.provider.removedTiles.empty());
        CHECK(f.manager.getStreamer().isTileLoaded({0, 0}));
    }

    TEST_CASE("prioritize only requests tiles that are not already loaded")
    {
        Fixture f;

        // All tiles already loaded: the request resolves immediately, and a later
        // release still finds the ref counts (prioritize counted them regardless)
        for (int x = 0; x <= 1; ++x)
            for (int z = 0; z <= 1; ++z)
                f.manager.getStreamer().markTileLoaded({x, z});

        world::SectorCoord sector{0, 0};
        f.manager.prioritizeTilesForBounds(sector, sectorMin(0.0f, 0.0f), sectorMax(64.0f, 64.0f));
        f.manager.releaseTilesForSector(sector, sectorMin(0.0f, 0.0f), sectorMax(64.0f, 64.0f));

        CHECK(f.provider.removedTiles.size() == 4);
    }
}
