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
        if (pendingSectorTileActions.empty() || !waterTileGrid)
            return;

        int loadsRemaining = 16;
        int unloadsRemaining = 16;

        float waterHeight = getBaseWaterHeight();

        auto it = pendingSectorTileActions.begin();
        while (it != pendingSectorTileActions.end())
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

            it = pendingSectorTileActions.erase(it);
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

        // Process all pending immediately for initial load
        processPendingSectorTileActions();
    }
}
