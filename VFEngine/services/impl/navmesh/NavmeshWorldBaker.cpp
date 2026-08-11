#include "NavmeshWorldBaker.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace services
{
    NavmeshWorldBaker::NavmeshWorldBaker(NavmeshTileManager& tileManagerRef, WorldOps worldOps)
        : tileManager(tileManagerRef), ops(std::move(worldOps))
    {
    }

    glm::vec3 NavmeshWorldBaker::sectorCenter(const ::world::SectorCoord& coord) const
    {
        return {
            (static_cast<float>(coord.x) + 0.5f) * sectorWorldSize,
            0.0f,
            (static_cast<float>(coord.z) + 0.5f) * sectorWorldSize
        };
    }

    bool NavmeshWorldBaker::isSectorReady(const ::world::SectorCoord& coord) const
    {
        auto readiness = ops.getReadiness(coord);
        // VK-1591: deliberately Loaded ONLY. A Prefetched sector holds bytes and no entities, so
        // there is no geometry to bake against.
        return readiness.state == ::world::SectorState::Loaded &&
               !readiness.fileLoadPending && !readiness.entitySpawnsPending;
    }

    bool NavmeshWorldBaker::start(const std::string& outputDirectory)
    {
        if (isRunning())
        {
            vfLogWarning("NavmeshWorldBaker: bake already in progress");
            return false;
        }

        sectorCoords = ops.getAllSectorCoords ? ops.getAllSectorCoords() : std::vector<::world::SectorCoord>{};
        if (sectorCoords.empty())
        {
            vfLogError("NavmeshWorldBaker: no sectors to bake (world mode required)");
            resultState = ::events::navmesh::WorldNavmeshBakeState::Failed;
            return false;
        }

        // Row-major order keeps neighborhood overlap high between consecutive sectors
        std::sort(sectorCoords.begin(), sectorCoords.end(),
                  [](const ::world::SectorCoord& a, const ::world::SectorCoord& b)
                  {
                      return a.z != b.z ? a.z < b.z : a.x < b.x;
                  });

        cursor = 0;
        tilesBaked = 0;
        settleFrames = 0;
        waitFrames = 0;
        submittedTiles.clear();
        bakerLoadedSectors.clear();
        tileQueue.clear();
        currentNeighborhood.clear();

        sectorWorldSize = ops.getSectorWorldSize ? ops.getSectorWorldSize() : 128.0f;

        tileManager.prepareTileCache(outputDirectory);
        tileManager.ensureTiledNavmeshInitialized();
        // A world bake exists to be streamed — default the toggle on; the user
        // can still turn it off in the Navigation window afterwards
        tileManager.getStreamer().setEnabled(true);

        if (ops.registerKeepAliveSource)
            keepAliveSourceId = ops.registerKeepAliveSource(sectorCenter(sectorCoords.front()));

        resultState = ::events::navmesh::WorldNavmeshBakeState::Baking;
        state = State::LoadNeighborhood;
        vfLogInfo("NavmeshWorldBaker: starting world bake over {} sectors -> {}", sectorCoords.size(), outputDirectory);
        return true;
    }

    void NavmeshWorldBaker::cancel()
    {
        if (!isRunning())
            return;
        finish(::events::navmesh::WorldNavmeshBakeState::Cancelled, false, "World navmesh bake cancelled");
    }

    void NavmeshWorldBaker::update()
    {
        switch (state)
        {
        case State::Idle: return;
        case State::LoadNeighborhood: stepLoadNeighborhood(); break;
        case State::WaitReady: stepWaitReady(); break;
        case State::BakeTiles: stepBakeTiles(); break;
        case State::WaitTileBakes: stepWaitTileBakes(); break;
        case State::AdvanceSector: stepAdvanceSector(); break;
        case State::WriteIndex: stepWriteIndex(); break;
        }
    }

    void NavmeshWorldBaker::stepLoadNeighborhood()
    {
        if (cursor >= sectorCoords.size())
        {
            state = State::WriteIndex;
            return;
        }

        const auto& coord = sectorCoords[cursor];

        if (keepAliveSourceId != 0 && ops.updateKeepAliveSource)
            ops.updateKeepAliveSource(keepAliveSourceId, sectorCenter(coord));

        // Border tiles overlap adjacent sectors, so their geometry must be present
        currentNeighborhood.clear();
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                ::world::SectorCoord neighbor{coord.x + dx, coord.z + dz};
                if (ops.sectorExists && !ops.sectorExists(neighbor))
                    continue;

                currentNeighborhood.push_back(neighbor);

                auto readiness = ops.getReadiness(neighbor);
                if (readiness.state == ::world::SectorState::Loaded ||
                    readiness.state == ::world::SectorState::Loading)
                    continue; // already (being) loaded by the world streamer
                // VK-1591: a Prefetched neighbour deliberately falls through to loadSector(),
                // which now activates from the cached bytes — a free speed-up, no change needed.

                if (ops.loadSector && ops.loadSector(neighbor))
                    bakerLoadedSectors.insert(neighbor);
            }
        }

        settleFrames = 0;
        waitFrames = 0;
        state = State::WaitReady;
    }

    void NavmeshWorldBaker::stepWaitReady()
    {
        bool allReady = true;
        for (const auto& neighbor : currentNeighborhood)
        {
            if (!isSectorReady(neighbor))
            {
                allReady = false;
                break;
            }
        }

        if (allReady)
        {
            // Terrain tiles activate off sector notifications a frame later —
            // give them a couple of frames before collecting geometry
            if (++settleFrames >= SETTLE_FRAMES)
            {
                buildTileQueueForSector(sectorCoords[cursor]);
                state = State::BakeTiles;
            }
            return;
        }

        settleFrames = 0;
        if (++waitFrames > MAX_WAIT_FRAMES)
        {
            vfLogWarning("NavmeshWorldBaker: sector ({}, {}) neighborhood never became ready — skipping",
                         sectorCoords[cursor].x, sectorCoords[cursor].z);
            state = State::AdvanceSector;
        }
    }

    void NavmeshWorldBaker::buildTileQueueForSector(const ::world::SectorCoord& coord)
    {
        glm::vec3 boundsMin(static_cast<float>(coord.x) * sectorWorldSize, -1000.0f,
                            static_cast<float>(coord.z) * sectorWorldSize);
        glm::vec3 boundsMax(static_cast<float>(coord.x + 1) * sectorWorldSize, 1000.0f,
                            static_cast<float>(coord.z + 1) * sectorWorldSize);

        for (const auto& tile : tileManager.computeTilesForBounds(boundsMin, boundsMax))
        {
            // Border tiles span two sectors — bake each exactly once
            if (submittedTiles.insert(tile).second)
                tileQueue.push_back(tile);
        }
    }

    void NavmeshWorldBaker::stepBakeTiles()
    {
        int submitted = 0;
        while (!tileQueue.empty() && submitted < MAX_BAKE_SUBMITS_PER_FRAME &&
               tileManager.getPendingBakeCount() < MAX_PENDING_BAKES)
        {
            auto tile = tileQueue.front();
            tileQueue.pop_front();

            // false = no geometry in this tile, nothing to bake
            if (tileManager.submitWorldBakeTile(tile))
            {
                ++tilesBaked;
                ++submitted;
            }
        }

        if (tileQueue.empty())
            state = State::WaitTileBakes;
    }

    void NavmeshWorldBaker::stepWaitTileBakes()
    {
        // Completions are polled by NavmeshServiceImpl::update each frame
        if (tileManager.getPendingBakeCount() == 0)
            state = State::AdvanceSector;
    }

    void NavmeshWorldBaker::stepAdvanceSector()
    {
        ++cursor;

        // Drop baker-loaded sectors the next neighborhood doesn't need
        std::unordered_set<::world::SectorCoord, ::world::SectorCoordHash> needed;
        if (cursor < sectorCoords.size())
        {
            const auto& next = sectorCoords[cursor];
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    needed.insert({next.x + dx, next.z + dz});
        }

        for (auto it = bakerLoadedSectors.begin(); it != bakerLoadedSectors.end();)
        {
            if (!needed.count(*it))
            {
                if (ops.unloadSector)
                    ops.unloadSector(*it);
                it = bakerLoadedSectors.erase(it);
            }
            else
            {
                ++it;
            }
        }

        state = State::LoadNeighborhood;
    }

    void NavmeshWorldBaker::stepWriteIndex()
    {
        bool ok = tileManager.finalizeWorldBakeIndex();
        if (ok)
            finish(::events::navmesh::WorldNavmeshBakeState::Complete, true, "World navmesh bake complete");
        else
            finish(::events::navmesh::WorldNavmeshBakeState::Failed, false, "Failed to write navmesh tile index");
    }

    void NavmeshWorldBaker::finish(::events::navmesh::WorldNavmeshBakeState result, bool success,
                                    const std::string& message)
    {
        if (keepAliveSourceId != 0 && ops.unregisterKeepAliveSource)
        {
            ops.unregisterKeepAliveSource(keepAliveSourceId);
            keepAliveSourceId = 0;
        }

        for (const auto& coord : bakerLoadedSectors)
        {
            if (ops.unloadSector)
                ops.unloadSector(coord);
        }
        bakerLoadedSectors.clear();
        tileQueue.clear();
        currentNeighborhood.clear();

        resultState = result;
        state = State::Idle;

        vfLogInfo("NavmeshWorldBaker: {} ({} tiles)", message, tilesBaked);
        if (onComplete)
            onComplete(success, message);
    }

    ::events::navmesh::WorldNavmeshBakeProgress NavmeshWorldBaker::getProgress() const
    {
        ::events::navmesh::WorldNavmeshBakeProgress progress;
        progress.state = resultState;
        progress.sectorsTotal = static_cast<int>(sectorCoords.size());
        progress.sectorsDone = static_cast<int>(std::min(cursor, sectorCoords.size()));
        progress.tilesBaked = tilesBaked;
        if (!sectorCoords.empty())
        {
            size_t idx = std::min(cursor, sectorCoords.size() - 1);
            progress.currentSectorX = sectorCoords[idx].x;
            progress.currentSectorZ = sectorCoords[idx].z;
        }
        return progress;
    }
}
