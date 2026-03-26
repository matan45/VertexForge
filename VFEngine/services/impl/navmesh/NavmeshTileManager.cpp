#include "NavmeshTileManager.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/HoleBrushEvents.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "threading/JobSystem.hpp"
#include <cmath>

namespace services
{
    NavmeshTileManager::NavmeshTileManager(INavmeshProvider* provider,
                                           const types::NavmeshBakeSettings& settings,
                                           CollectGeometryFunc collectTileGeometryFunc)
        : navmeshProvider(provider), bakeSettings(settings),
          collectTileGeometry(std::move(collectTileGeometryFunc))
    {
    }

    void NavmeshTileManager::registerEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        brushAppliedToken = dispatcher.subscribe<events::brush::BrushAppliedNotification>(
            [this](const events::brush::BrushAppliedNotification& notif)
            {
                if (!navmeshProvider->hasNavmesh())
                    return;
                auto coord = worldToTileCoord(notif.position);
                markTileDirty(coord.x, coord.z);
                markTileDirty(coord.x - 1, coord.z);
                markTileDirty(coord.x + 1, coord.z);
                markTileDirty(coord.x, coord.z - 1);
                markTileDirty(coord.x, coord.z + 1);
            });

        holeBrushAppliedToken = dispatcher.subscribe<events::holeBrush::HoleBrushAppliedNotification>(
            [this](const events::holeBrush::HoleBrushAppliedNotification& notif)
            {
                if (!navmeshProvider->hasNavmesh())
                    return;
                auto coord = worldToTileCoord(notif.position);
                markTileDirty(coord.x, coord.z);
                markTileDirty(coord.x - 1, coord.z);
                markTileDirty(coord.x + 1, coord.z);
                markTileDirty(coord.x, coord.z - 1);
                markTileDirty(coord.x, coord.z + 1);
            });
    }

    void NavmeshTileManager::unregisterEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        if (brushAppliedToken.isValid())
            dispatcher.unsubscribe(brushAppliedToken);
        if (holeBrushAppliedToken.isValid())
            dispatcher.unsubscribe(holeBrushAppliedToken);
    }

    navigation::NavmeshTileCoord NavmeshTileManager::worldToTileCoord(const glm::vec3& worldPos) const
    {
        float tileWorldSize = bakeSettings.tileSize * bakeSettings.cellSize;
        return {
            static_cast<int32_t>(std::floor(worldPos.x / tileWorldSize)),
            static_cast<int32_t>(std::floor(worldPos.z / tileWorldSize))
        };
    }

    void NavmeshTileManager::markTileDirty(int tileX, int tileZ)
    {
        dirtyTiles.insert({tileX, tileZ});
    }

    bool NavmeshTileManager::bakeSingleTile(int tileX, int tileZ)
    {
        if (!navmeshProvider->hasNavmesh())
            return false;

        navigation::NavmeshTileCoord coord{tileX, tileZ};
        auto bounds = navigation::computeTileBounds(coord, bakeSettings, -1000.0f, 1000.0f);

        navigation::NavmeshInputGeometry geometry;
        collectTileGeometry(bounds, bakeSettings, geometry);

        if (geometry.isEmpty())
        {
            navmeshProvider->removeNavmeshTile(tileX, tileZ);
            return true;
        }

        navigation::NavmeshOffMeshConnections offMeshLinks;
        if (collectOffMeshLinks)
            offMeshLinks = collectOffMeshLinks(bounds, bakeSettings);

        std::vector<navigation::NavmeshAreaModifier> areaModifiers;
        if (collectAreaModifiers)
            areaModifiers = collectAreaModifiers(bounds);

        auto tileData = navmeshProvider->buildSingleTile(tileX, tileZ, geometry, bakeSettings, offMeshLinks, areaModifiers);
        if (tileData.data.empty())
            return false;

        navmeshProvider->addNavmeshTile(tileData);

        if (tileCache)
        {
            tileCache->saveTile(coord, tileData);
        }

        dirtyTiles.erase(coord);

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::navmesh::NavmeshTileUpdatedNotification notif;
        notif.tileX = tileX;
        notif.tileZ = tileZ;
        dispatcher.publish(notif);

        return true;
    }

    bool NavmeshTileManager::saveNavmeshTiled(const std::string& directory)
    {
        if (!navmeshProvider->hasNavmesh())
            return false;

        tileCache = std::make_unique<navigation::NavmeshTileCache>(directory);

        auto tiles = navmeshProvider->serializeNavmesh();

        navigation::NavmeshTileIndex index;
        index.settings = bakeSettings;

        for (const auto& tile : tiles)
        {
            navigation::NavmeshTileCoord coord{tile.x, tile.y};
            tileCache->saveTile(coord, tile);
            index.tileCoords.push_back(coord);
        }

        if (!tiles.empty())
        {
            float tileWorldSize = bakeSettings.tileSize * bakeSettings.cellSize;
            float minX = 1e9f, minZ = 1e9f, maxX = -1e9f, maxZ = -1e9f;
            for (const auto& coord : index.tileCoords)
            {
                minX = std::min(minX, coord.x * tileWorldSize);
                minZ = std::min(minZ, coord.z * tileWorldSize);
                maxX = std::max(maxX, (coord.x + 1) * tileWorldSize);
                maxZ = std::max(maxZ, (coord.z + 1) * tileWorldSize);
            }
            index.boundsMin = glm::vec3(minX, -1000.0f, minZ);
            index.boundsMax = glm::vec3(maxX, 1000.0f, maxZ);
        }

        tileCache->saveIndex(index);

        streamer.setTileCache(tileCache.get());
        streamer.setProvider(navmeshProvider);
        streamer.setSettings(bakeSettings);

        vfLogInfo("NavmeshService: Saved {} tiles to {}", tiles.size(), directory);
        return true;
    }

    bool NavmeshTileManager::loadNavmeshTiled(const std::string& directory, types::NavmeshBakeSettings& outSettings)
    {
        tileCache = std::make_unique<navigation::NavmeshTileCache>(directory);

        navigation::NavmeshTileIndex index;
        if (!tileCache->loadIndex(index))
        {
            vfLogError("NavmeshService: Failed to load navmesh tile index from {}", directory);
            return false;
        }

        outSettings = index.settings;

        if (!navmeshProvider->initTiledNavmesh(index.settings, index.boundsMin, index.boundsMax))
        {
            vfLogError("NavmeshService: Failed to init tiled navmesh");
            return false;
        }

        int loadedCount = 0;
        for (const auto& coord : index.tileCoords)
        {
            navigation::NavmeshTileData tileData;
            if (tileCache->loadTile(coord, tileData))
            {
                if (navmeshProvider->addNavmeshTile(tileData))
                {
                    loadedCount++;
                }
            }
        }

        streamer.setTileCache(tileCache.get());
        streamer.setProvider(navmeshProvider);
        streamer.setSettings(index.settings);
        streamer.setEnabled(true);

        for (const auto& coord : index.tileCoords)
        {
            streamer.markTileLoaded(coord);
        }

        vfLogInfo("NavmeshService: Loaded {} / {} tiles from {}", loadedCount, index.tileCoords.size(), directory);

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::navmesh::NavmeshBakeCompleteNotification notification;
        notification.success = true;
        notification.message = "Tiled navmesh loaded";
        dispatcher.publish(notification);

        return true;
    }

    StreamingResult NavmeshTileManager::updateStreaming()
    {
        StreamingResult result;

        if (!streamer.isEnabled())
            return result;

        streamer.update(lastCameraPos, result.loaded, result.unloaded);

        auto& dispatcher = ::events::EventDispatcher::instance();

        for (const auto& coord : result.loaded)
        {
            events::navmesh::NavmeshTileLoadedNotification notif;
            notif.tileX = coord.x;
            notif.tileZ = coord.z;
            dispatcher.publish(notif);
        }

        for (const auto& coord : result.unloaded)
        {
            events::navmesh::NavmeshTileUnloadedNotification notif;
            notif.tileX = coord.x;
            notif.tileZ = coord.z;
            dispatcher.publish(notif);
        }

        return result;
    }

    void NavmeshTileManager::processDirtyTiles()
    {
        if (dirtyTiles.empty())
            return;

        pollTileBakeCompletions();

        int submitted = 0;
        auto it = dirtyTiles.begin();
        while (it != dirtyTiles.end() && submitted < MAX_TILE_BAKES_PER_FRAME)
        {
            if (static_cast<int>(pendingTileBakes.size()) >= MAX_TILE_BAKES_PER_FRAME)
                break;

            navigation::NavmeshTileCoord coord = *it;
            it = dirtyTiles.erase(it);

            auto bounds = navigation::computeTileBounds(coord, bakeSettings, -1000.0f, 1000.0f);

            navigation::NavmeshInputGeometry geometry;
            collectTileGeometry(bounds, bakeSettings, geometry);

            if (geometry.isEmpty())
            {
                navmeshProvider->removeNavmeshTile(coord.x, coord.z);
                continue;
            }

            navigation::NavmeshOffMeshConnections offMeshLinks;
            if (collectOffMeshLinks)
                offMeshLinks = collectOffMeshLinks(bounds, bakeSettings);

            std::vector<navigation::NavmeshAreaModifier> areaModifiers;
            if (collectAreaModifiers)
                areaModifiers = collectAreaModifiers(bounds);

            auto future = threading::JobSystem::instance().submit(
                [this, coord, geom = std::move(geometry), settings = bakeSettings, links = std::move(offMeshLinks), mods = std::move(areaModifiers)]()
                {
                    return navmeshProvider->buildSingleTile(coord.x, coord.z, geom, settings, links, mods);
                }, threading::JobPriority::NORMAL);

            pendingTileBakes.push_back({coord, std::move(future)});
            submitted++;
        }
    }

    void NavmeshTileManager::pollTileBakeCompletions()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto it = pendingTileBakes.begin();
        while (it != pendingTileBakes.end())
        {
            if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto tileData = it->future.get();
                if (!tileData.data.empty())
                {
                    navmeshProvider->addNavmeshTile(tileData);

                    if (tileCache)
                    {
                        tileCache->saveTile(it->coord, tileData);
                    }

                    events::navmesh::NavmeshTileUpdatedNotification notif;
                    notif.tileX = it->coord.x;
                    notif.tileZ = it->coord.z;
                    dispatcher.publish(notif);
                }

                it = pendingTileBakes.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void NavmeshTileManager::clear()
    {
        for (auto& pending : pendingTileBakes)
        {
            if (pending.future.valid())
                pending.future.wait();
        }
        dirtyTiles.clear();
        pendingTileBakes.clear();
        streamer.clear();
        tileCache.reset();
    }
}
