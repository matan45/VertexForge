#include <doctest.h>
#include <impl/navmesh/NavmeshTileManager.hpp>
#include <providers/navmesh/INavmeshProvider.hpp>
#include <types/NavmeshTypes.hpp>

#include <filesystem>
#include <string>

// ============================================================
// VK-1422: runtime navmesh tile rebuilds (obstacle carving,
// on-demand streaming) must stay in-memory during play and must
// NOT write back to the authored .vfNavTile assets. The dirty-tile
// rebuild still applies in memory (addNavmeshTile) either way.
//
// The JobSystem is uninitialized here, so submitTileBake runs the
// bake inline; pollTileBakeCompletions() then applies it and, when
// permitted, writes the tile to the cache directory.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // Minimal provider: records how many tiles were applied in-memory.
    class CountingProvider : public services::INavmeshProvider
    {
    public:
        int addedTiles = 0;

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
        navigation::NavmeshTileData buildSingleTile(int tx, int tz,
                                                    const navigation::NavmeshInputGeometry&,
                                                    const types::NavmeshBakeSettings&,
                                                    const navigation::NavmeshOffMeshConnections&,
                                                    const std::vector<navigation::NavmeshAreaModifier>&,
                                                    uint8_t) override
        {
            navigation::NavmeshTileData tile;
            tile.x = tx;
            tile.y = tz;
            tile.data = {1, 2, 3, 4};
            tile.dataSize = 4;
            return tile;
        }
        bool addNavmeshTile(const navigation::NavmeshTileData&) override
        {
            ++addedTiles;
            return true;
        }
        bool removeNavmeshTile(int, int) override { return true; }

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
    };

    // Non-empty geometry so processDirtyTiles bakes (instead of taking the
    // empty-geometry -> removeNavmeshTile branch).
    services::NavmeshTileManager::CollectGeometryFunc makeTriangleGeometry()
    {
        return [](const navigation::NavmeshTileBounds& bounds,
                  const types::NavmeshBakeSettings&,
                  navigation::NavmeshInputGeometry& outGeometry)
        {
            outGeometry.addVertex(bounds.min);
            outGeometry.addVertex({bounds.max.x, bounds.min.y, bounds.min.z});
            outGeometry.addVertex({bounds.min.x, bounds.min.y, bounds.max.z});
            outGeometry.addTriangle(0, 1, 2);
        };
    }

    // Rebuilds tile (3,4) through the dirty-tile path and reports whether the
    // tile file landed on disk. The tile is applied in-memory regardless.
    bool rebuildDirtyTileWritesToDisk(bool playMode, const fs::path& dir, int& addedTilesOut)
    {
        fs::remove_all(dir);

        CountingProvider provider;
        types::NavmeshBakeSettings settings;
        services::NavmeshTileManager manager(&provider, settings, makeTriangleGeometry());

        manager.prepareTileCache(dir.string());
        manager.setPlayModeActive(playMode);

        manager.markTileDirty(3, 4);
        manager.processDirtyTiles();        // bakes inline (JobSystem uninitialized)
        manager.pollTileBakeCompletions();  // applies + (maybe) saves to disk

        addedTilesOut = provider.addedTiles;
        return fs::exists(dir / "tile_3_4.vfNavTile");
    }
}

TEST_SUITE("NavmeshPlayIsolation")
{
    TEST_CASE("play-mode tile rebuild stays in-memory and does not touch disk")
    {
        fs::path dir = fs::temp_directory_path() / "vf_test_nav_play_isolation_play";

        int addedTiles = 0;
        bool wroteToDisk = rebuildDirtyTileWritesToDisk(/*playMode=*/true, dir, addedTiles);

        CHECK_FALSE(wroteToDisk); // VK-1422: no .vfNavTile written during play
        CHECK(addedTiles == 1);   // but the rebuild still applied in memory

        fs::remove_all(dir);
    }

    TEST_CASE("edit-mode tile rebuild persists the tile to disk (authoring unchanged)")
    {
        fs::path dir = fs::temp_directory_path() / "vf_test_nav_play_isolation_edit";

        int addedTiles = 0;
        bool wroteToDisk = rebuildDirtyTileWritesToDisk(/*playMode=*/false, dir, addedTiles);

        CHECK(wroteToDisk);     // edit-mode brush authoring still saves
        CHECK(addedTiles == 1); // and applies in memory

        fs::remove_all(dir);
    }
}
