#pragma once

#include "NavmeshTileManager.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "world/WorldTypes.hpp"
#include <glm/glm.hpp>
#include <deque>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace services
{
    // Sector-by-sector whole-world navmesh bake. The whole world is never loaded
    // at once in world mode, so the baker walks the sector list, holds each
    // sector's 3x3 neighborhood loaded (border tiles need neighbor geometry),
    // bakes that sector's tiles async through the tile manager, and writes the
    // tile-cache index at the end. Pumped once per frame from NavmeshServiceImpl
    // (edit mode only — entering play mode cancels the bake).
    class NavmeshWorldBaker
    {
    public:
        // Dispatcher seams — wired to EventDispatcher by NavmeshServiceImpl,
        // stubbed in unit tests
        struct WorldOps
        {
            std::function<std::vector<::world::SectorCoord>()> getAllSectorCoords;
            std::function<bool(const ::world::SectorCoord&)> loadSector;
            std::function<bool(const ::world::SectorCoord&)> unloadSector;
            std::function<bool(const ::world::SectorCoord&)> sectorExists;
            std::function<::events::world::SectorReadiness(const ::world::SectorCoord&)> getReadiness;
            std::function<float()> getSectorWorldSize;
            // Keep-alive streaming source pinned to the bake cursor so the world
            // streamer never unloads the neighborhood mid-bake (optional)
            std::function<uint32_t(const glm::vec3&)> registerKeepAliveSource;
            std::function<void(uint32_t, const glm::vec3&)> updateKeepAliveSource;
            std::function<void(uint32_t)> unregisterKeepAliveSource;
        };

        NavmeshWorldBaker(NavmeshTileManager& tileManager, WorldOps ops);

        bool start(const std::string& outputDirectory);
        void cancel();
        void update();

        [[nodiscard]] bool isRunning() const { return state != State::Idle; }
        [[nodiscard]] ::events::navmesh::WorldNavmeshBakeProgress getProgress() const;

        void setCompletionCallback(std::function<void(bool, const std::string&)> callback)
        {
            onComplete = std::move(callback);
        }

    private:
        enum class State
        {
            Idle = 0,
            LoadNeighborhood,
            WaitReady,
            BakeTiles,
            WaitTileBakes,
            AdvanceSector,
            WriteIndex
        };

        static constexpr int SETTLE_FRAMES = 2;          // post-ready frames for terrain tile activation
        static constexpr int MAX_WAIT_FRAMES = 1800;     // ~30s @60fps before skipping a stuck sector
        static constexpr int MAX_BAKE_SUBMITS_PER_FRAME = 4;
        static constexpr int MAX_PENDING_BAKES = 8;

        NavmeshTileManager& tileManager;
        WorldOps ops;

        State state = State::Idle;
        ::events::navmesh::WorldNavmeshBakeState resultState = ::events::navmesh::WorldNavmeshBakeState::Idle;

        std::vector<::world::SectorCoord> sectorCoords;
        size_t cursor = 0;
        std::vector<::world::SectorCoord> currentNeighborhood;
        std::unordered_set<::world::SectorCoord, ::world::SectorCoordHash> bakerLoadedSectors;
        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> submittedTiles;
        std::deque<navigation::NavmeshTileCoord> tileQueue;

        uint32_t keepAliveSourceId = 0;
        float sectorWorldSize = 128.0f;
        int settleFrames = 0;
        int waitFrames = 0;
        int tilesBaked = 0;

        std::function<void(bool, const std::string&)> onComplete;

        void stepLoadNeighborhood();
        void stepWaitReady();
        void stepBakeTiles();
        void stepWaitTileBakes();
        void stepAdvanceSector();
        void stepWriteIndex();

        void buildTileQueueForSector(const ::world::SectorCoord& coord);
        void finish(::events::navmesh::WorldNavmeshBakeState result, bool success, const std::string& message);
        [[nodiscard]] glm::vec3 sectorCenter(const ::world::SectorCoord& coord) const;
        [[nodiscard]] bool isSectorReady(const ::world::SectorCoord& coord) const;
    };
}
