#include "OceanService.hpp"
#include "../../../utilities/water/WaterTileGrid.hpp"
#include "../../events/world/WorldSectorEvents.hpp"

namespace services
{
    const water::WaterTileGrid* OceanService::getWaterTileGrid() const
    {
        return waterTileGrid.get();
    }

    void OceanService::onSectorActivated(const world::SectorCoord& coord, const world::SectorConfig& config)
    {
        if (!oceanEntity.isValid())
            return;

        int32_t tps = config.tilesPerSector;
        std::lock_guard<std::mutex> lock(pendingActionsMutex);

        for (int32_t tx = coord.x * tps; tx < (coord.x + 1) * tps; ++tx)
        {
            for (int32_t tz = coord.z * tps; tz < (coord.z + 1) * tps; ++tz)
            {
                terrain::TileCoord tileCoord{tx, tz};
                if (!waterTileGrid || !waterTileGrid->hasTile(tileCoord))
                {
                    pendingSectorTileActions.push_back({tileCoord, true});
                }
            }
        }
    }

    void OceanService::onSectorDeactivated(const world::SectorCoord& coord, const world::SectorConfig& config)
    {
        if (!oceanEntity.isValid())
            return;

        int32_t tps = config.tilesPerSector;
        std::lock_guard<std::mutex> lock(pendingActionsMutex);

        for (int32_t tx = coord.x * tps; tx < (coord.x + 1) * tps; ++tx)
        {
            for (int32_t tz = coord.z * tps; tz < (coord.z + 1) * tps; ++tz)
            {
                terrain::TileCoord tileCoord{tx, tz};
                if (waterTileGrid && waterTileGrid->hasTile(tileCoord))
                {
                    pendingSectorTileActions.push_back({tileCoord, false});
                }
            }
        }
    }

    void OceanService::processPendingSectorTileActions()
    {
        if (!waterTileGrid)
            return;

        // Swap pending actions under lock to minimize lock duration
        std::vector<PendingWaterTileAction> actions;
        {
            std::lock_guard<std::mutex> lock(pendingActionsMutex);
            if (pendingSectorTileActions.empty())
                return;
            actions.swap(pendingSectorTileActions);
        }

        int loadsRemaining = 16;
        int unloadsRemaining = 16;

        float waterHeight = getBaseWaterHeight();

        auto it = actions.begin();
        while (it != actions.end())
        {
            if (it->isLoad && loadsRemaining <= 0)
            {
                ++it;
                continue;
            }
            if (!it->isLoad && unloadsRemaining <= 0)
            {
                ++it;
                continue;
            }

            if (it->isLoad)
            {
                waterTileGrid->addTile(it->coord, waterHeight);
                --loadsRemaining;
            }
            else
            {
                waterTileGrid->removeTile(it->coord);
                --unloadsRemaining;
            }

            it = actions.erase(it);
        }

        // Put unprocessed actions back
        if (!actions.empty())
        {
            std::lock_guard<std::mutex> lock(pendingActionsMutex);
            pendingSectorTileActions.insert(pendingSectorTileActions.begin(),
                                            actions.begin(), actions.end());
        }
    }

    void OceanService::activateWaterTilesForLoadedSectors()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        bool isWorld = dispatcher.query(::events::world::IsWorldModeQuery{});
        worldModeActive = isWorld;

        if (!isWorld)
        {
            if (waterTileGrid)
                waterTileGrid->clear();
            return;
        }

        if (!waterTileGrid)
            waterTileGrid = std::make_unique<water::WaterTileGrid>();

        cachedSectorConfig = dispatcher.query(::events::world::GetSectorConfigQuery{});

        auto loadedCoords = dispatcher.query(::events::world::GetLoadedSectorCoordsQuery{});
        for (const auto& coord : loadedCoords)
        {
            onSectorActivated(coord, cachedSectorConfig);
        }

        // Initial load uses the same per-frame throttle — no special bypass
        // Tiles will stream in over multiple frames via processPendingSectorTileActions()
    }
}
