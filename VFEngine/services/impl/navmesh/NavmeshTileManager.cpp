#include "NavmeshTileManager.hpp"
#include "resource/VirtualFileSystem.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/HoleBrushEvents.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "threading/JobSystem.hpp"
#include <cmath>

namespace services
{
    namespace
    {
        ::events::navmesh::NavmeshStreamingConfig toStreamingConfig(const navigation::NavmeshIndexStreamingSettings& s)
        {
            ::events::navmesh::NavmeshStreamingConfig cfg;
            cfg.loadRadius = s.loadRadius;
            cfg.unloadRadius = s.unloadRadius;
            cfg.maxLoadsPerFrame = s.maxLoadsPerFrame;
            cfg.maxUnloadsPerFrame = s.maxUnloadsPerFrame;
            for (int i = 0; i < 3; ++i)
                cfg.lodDistances[i] = s.lodDistances[i];
            return cfg;
        }

        navigation::NavmeshIndexStreamingSettings toIndexStreamingSettings(
            const ::events::navmesh::NavmeshStreamingConfig& cfg, bool enabled)
        {
            navigation::NavmeshIndexStreamingSettings s;
            s.enabled = enabled ? 1 : 0;
            s.loadRadius = cfg.loadRadius;
            s.unloadRadius = cfg.unloadRadius;
            s.maxLoadsPerFrame = cfg.maxLoadsPerFrame;
            s.maxUnloadsPerFrame = cfg.maxUnloadsPerFrame;
            for (int i = 0; i < 3; ++i)
                s.lodDistances[i] = cfg.lodDistances[i];
            return s;
        }
    }

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

        // Sector-driven navmesh pre-loading
        sectorAboutToLoadToken = dispatcher.subscribe<::events::world::SectorAboutToLoadNotification>(
            [this](const ::events::world::SectorAboutToLoadNotification& notif)
            {
                if (!streamer.isEnabled())
                    return;
                prioritizeTilesForBounds(notif.coord, notif.boundsMin, notif.boundsMax);
            });

        // Coordinated unload
        sectorUnloadedToken = dispatcher.subscribe<::events::world::SectorUnloadedNotification>(
            [this](const ::events::world::SectorUnloadedNotification& notif)
            {
                if (!streamer.isEnabled())
                    return;
                float sectorSize = notif.sectorConfig.sectorWorldSize;
                glm::vec3 boundsMin(
                    static_cast<float>(notif.coord.x) * sectorSize, -1000.0f,
                    static_cast<float>(notif.coord.z) * sectorSize);
                glm::vec3 boundsMax(
                    static_cast<float>(notif.coord.x + 1) * sectorSize, 1000.0f,
                    static_cast<float>(notif.coord.z + 1) * sectorSize);
                releaseTilesForSector(notif.coord, boundsMin, boundsMax);
            });
    }

    void NavmeshTileManager::unregisterEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        if (brushAppliedToken.isValid())
            dispatcher.unsubscribe(brushAppliedToken);
        if (holeBrushAppliedToken.isValid())
            dispatcher.unsubscribe(holeBrushAppliedToken);
        if (sectorAboutToLoadToken.isValid())
            dispatcher.unsubscribe(sectorAboutToLoadToken);
        if (sectorUnloadedToken.isValid())
            dispatcher.unsubscribe(sectorUnloadedToken);
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
        index.streaming = toIndexStreamingSettings(streamer.getConfig(), streamer.isEnabled());

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

        // Build and save LOD 1/2 variants if multi-LOD is configured
        if (bakeSettings.lodConfig.lodCount > 1 && collectTileGeometry)
        {
            for (const auto& coord : index.tileCoords)
            {
                auto bounds = navigation::computeTileBounds(coord, bakeSettings, -1000.0f, 1000.0f);

                navigation::NavmeshInputGeometry geometry;
                collectTileGeometry(bounds, bakeSettings, geometry);
                if (geometry.isEmpty())
                    continue;

                navigation::NavmeshOffMeshConnections offMeshLinks;
                if (collectOffMeshLinks)
                    offMeshLinks = collectOffMeshLinks(bounds, bakeSettings);

                std::vector<navigation::NavmeshAreaModifier> areaModifiers;
                if (collectAreaModifiers)
                    areaModifiers = collectAreaModifiers(bounds);

                for (uint8_t lod = 1; lod < bakeSettings.lodConfig.lodCount; ++lod)
                {
                    auto lodTileData = navmeshProvider->buildSingleTile(
                        coord.x, coord.z, geometry, bakeSettings, offMeshLinks, areaModifiers, lod);

                    if (!lodTileData.data.empty())
                    {
                        navigation::NavmeshTileLodKey lodKey{coord.x, coord.z, lod};
                        tileCache->saveTile(lodKey, lodTileData);
                    }
                }
            }
            vfLogInfo("NavmeshService: Built LOD variants for {} tiles", index.tileCoords.size());
        }

        streamer.setTileCache(tileCache.get());
        streamer.setProvider(navmeshProvider);
        streamer.setSettings(bakeSettings);
        streamer.setLodConfig(bakeSettings.lodConfig);

        vfLogInfo("NavmeshService: Saved {} tiles to {}", tiles.size(), directory);
        return true;
    }

    bool NavmeshTileManager::loadNavmeshTiled(const std::string& directory, types::NavmeshBakeSettings& outSettings)
    {
        tileCache = std::make_unique<navigation::NavmeshTileCache>(directory);

        // Shipped builds read tiles from the pak; nothing can be written back
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            saveOnDemandToCache = false;

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
        tiledNavmeshInitialized = true;

        streamer.setTileCache(tileCache.get());
        streamer.setProvider(navmeshProvider);
        streamer.setSettings(index.settings);
        streamer.setLodConfig(index.settings.lodConfig);
        streamer.setConfig(toStreamingConfig(index.streaming));
        streamer.setMaxResidentTiles(navmeshProvider->getMaxResidentTiles());

        const bool streamingOn = index.streaming.enabled != 0;
        streamer.setEnabled(streamingOn);

        if (streamingOn)
        {
            // Deferred load: tiles stream in around camera/invokers/sectors.
            // The first updateStreaming() pulls a larger burst so the spawn
            // area isn't empty for the opening frames.
            initialLoadBurst = INITIAL_STREAM_LOAD_BURST;
            vfLogInfo("NavmeshService: Tiled navmesh ready, streaming {} tiles on demand from {}",
                      index.tileCoords.size(), directory);
        }
        else
        {
            int loadedCount = 0;
            for (const auto& coord : index.tileCoords)
            {
                navigation::NavmeshTileData tileData;
                if (tileCache->loadTile(coord, tileData))
                {
                    if (navmeshProvider->addNavmeshTile(tileData))
                    {
                        loadedCount++;
                        streamer.markTileLoaded(coord);
                    }
                }
            }
            vfLogInfo("NavmeshService: Loaded {} / {} tiles from {}", loadedCount, index.tileCoords.size(), directory);
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::navmesh::NavmeshBakeCompleteNotification notification;
        notification.success = true;
        notification.message = streamingOn ? "Tiled navmesh loaded (streaming)" : "Tiled navmesh loaded";
        dispatcher.publish(notification);

        return true;
    }

    int NavmeshTileManager::loadAllTilesFromCache()
    {
        if (!tileCache)
            return 0;

        int loadedCount = 0;
        auto& dispatcher = ::events::EventDispatcher::instance();

        tileCache->forEachTile([&](const navigation::NavmeshTileCoord& coord)
        {
            if (streamer.isTileLoaded(coord))
                return;

            navigation::NavmeshTileData tileData;
            if (tileCache->loadTile(coord, tileData) && navmeshProvider->addNavmeshTile(tileData))
            {
                streamer.markTileLoaded(coord);
                loadedCount++;

                events::navmesh::NavmeshTileLoadedNotification notif;
                notif.tileX = coord.x;
                notif.tileZ = coord.z;
                dispatcher.publish(notif);
            }
        });

        if (loadedCount > 0)
            vfLogInfo("NavmeshService: Loaded {} remaining tiles from cache", loadedCount);
        return loadedCount;
    }

    void NavmeshTileManager::setInvokerSources(std::vector<StreamingSource> sources)
    {
        invokerSources = std::move(sources);
    }

    void NavmeshTileManager::prepareTileCache(const std::string& directory)
    {
        tileCache = std::make_unique<navigation::NavmeshTileCache>(directory);
        streamer.setTileCache(tileCache.get());
    }

    bool NavmeshTileManager::submitWorldBakeTile(const navigation::NavmeshTileCoord& coord)
    {
        if (!collectTileGeometry)
            return false;

        auto bounds = navigation::computeTileBounds(coord, bakeSettings, -1000.0f, 1000.0f);

        navigation::NavmeshInputGeometry geometry;
        collectTileGeometry(bounds, bakeSettings, geometry);
        if (geometry.isEmpty())
            return false;

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
            }, threading::JobPriority::LOW);

        pendingTileBakes.push_back({coord, true, std::move(future)});
        return true;
    }

    bool NavmeshTileManager::finalizeWorldBakeIndex()
    {
        if (!tileCache)
            return false;

        navigation::NavmeshTileIndex index;
        index.settings = bakeSettings;
        index.streaming = toIndexStreamingSettings(streamer.getConfig(), streamer.isEnabled());

        tileCache->forEachTile([&](const navigation::NavmeshTileCoord& coord)
        {
            index.tileCoords.push_back(coord);
        });

        if (!index.tileCoords.empty())
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

        return tileCache->saveIndex(index);
    }

    void NavmeshTileManager::ensureTiledNavmeshInitialized()
    {
        if (tiledNavmeshInitialized || navmeshProvider->hasNavmesh())
        {
            tiledNavmeshInitialized = true;
            return;
        }

        glm::vec3 boundsMin{-10000.0f, -1000.0f, -10000.0f};
        glm::vec3 boundsMax{10000.0f, 1000.0f, 10000.0f};
        if (navmeshProvider->initTiledNavmesh(bakeSettings, boundsMin, boundsMax))
        {
            tiledNavmeshInitialized = true;
            streamer.setProvider(navmeshProvider);
            streamer.setSettings(bakeSettings);
            streamer.setLodConfig(bakeSettings.lodConfig);
            streamer.setMaxResidentTiles(navmeshProvider->getMaxResidentTiles());
            streamer.setEnabled(true);
        }
    }

    StreamingResult NavmeshTileManager::updateStreaming()
    {
        StreamingResult result;

        if (!streamer.isEnabled())
            return result;

        // Process sector-driven tile requests with priority before normal streaming
        processSectorTileRequests();

        // One-shot burst after a deferred (streaming) load: raise the per-frame
        // load cap once so the area around the first sources fills immediately
        ::events::navmesh::NavmeshStreamingConfig savedConfig;
        const bool burst = initialLoadBurst > 0;
        if (burst)
        {
            savedConfig = streamer.getConfig();
            auto boosted = savedConfig;
            boosted.maxLoadsPerFrame = initialLoadBurst;
            streamer.setConfig(boosted);
            initialLoadBurst = 0;
        }

        if (!invokerSources.empty())
        {
            std::vector<navigation::NavmeshTileCoord> needGeneration;
            streamer.update(invokerSources, result.loaded, result.unloaded, needGeneration);

            for (const auto& coord : needGeneration)
                pendingGenerationTiles.insert(coord);
        }
        else
        {
            streamer.update(lastCameraPos, result.loaded, result.unloaded);
        }

        if (burst)
            streamer.setConfig(savedConfig);

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

    void NavmeshTileManager::processOnDemandGeneration()
    {
        if (pendingGenerationTiles.empty() || !collectTileGeometry)
            return;

        int submitted = 0;
        auto it = pendingGenerationTiles.begin();
        while (it != pendingGenerationTiles.end() && submitted < MAX_TILE_BAKES_PER_FRAME)
        {
            if (static_cast<int>(pendingTileBakes.size()) >= MAX_TILE_BAKES_PER_FRAME)
                break;

            navigation::NavmeshTileCoord coord = *it;
            it = pendingGenerationTiles.erase(it);

            auto bounds = navigation::computeTileBounds(coord, bakeSettings, -1000.0f, 1000.0f);

            navigation::NavmeshInputGeometry geometry;
            collectTileGeometry(bounds, bakeSettings, geometry);

            if (geometry.isEmpty())
                continue;

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
                }, threading::JobPriority::LOW);

            pendingTileBakes.push_back({coord, saveOnDemandToCache, std::move(future)});
            submitted++;
        }
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

            pendingTileBakes.push_back({coord, true, std::move(future)});
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

                    // Mark as loaded in streamer (for on-demand generated tiles)
                    if (!streamer.isTileLoaded(it->coord))
                        streamer.markTileGenerated(it->coord);

                    if (tileCache && it->saveToCache)
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
        pendingGenerationTiles.clear();
        pendingTileBakes.clear();
        pendingSectorTileRequests.clear();
        sectorRefCounts.clear();
        invokerSources.clear();
        initialLoadBurst = 0;
        tiledNavmeshInitialized = false;
        streamer.clear();
        tileCache.reset();
    }

    std::vector<navigation::NavmeshTileCoord> NavmeshTileManager::computeTilesForBounds(
        const glm::vec3& boundsMin, const glm::vec3& boundsMax) const
    {
        float tileWorldSize = bakeSettings.tileSize * bakeSettings.cellSize;
        int minTileX = static_cast<int>(std::floor(boundsMin.x / tileWorldSize));
        int maxTileX = static_cast<int>(std::floor((boundsMax.x - 0.001f) / tileWorldSize));
        int minTileZ = static_cast<int>(std::floor(boundsMin.z / tileWorldSize));
        int maxTileZ = static_cast<int>(std::floor((boundsMax.z - 0.001f) / tileWorldSize));

        std::vector<navigation::NavmeshTileCoord> result;
        result.reserve((maxTileX - minTileX + 1) * (maxTileZ - minTileZ + 1));
        for (int x = minTileX; x <= maxTileX; ++x)
            for (int z = minTileZ; z <= maxTileZ; ++z)
                result.push_back({x, z});
        return result;
    }

    void NavmeshTileManager::prioritizeTilesForBounds(const world::SectorCoord& sectorCoord,
                                                       const glm::vec3& boundsMin,
                                                       const glm::vec3& boundsMax)
    {
        auto tiles = computeTilesForBounds(boundsMin, boundsMax);

        SectorTileRequest request;
        request.sectorCoord = sectorCoord;

        for (const auto& coord : tiles)
        {
            sectorRefCounts[coord]++;

            if (!streamer.isTileLoaded(coord))
                request.tileCoords.push_back(coord);
        }

        if (!request.tileCoords.empty())
            pendingSectorTileRequests.push_back(std::move(request));
    }

    void NavmeshTileManager::processSectorTileRequests()
    {
        if (pendingSectorTileRequests.empty() || !tileCache)
            return;

        int loaded = 0;
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto it = pendingSectorTileRequests.begin();
        while (it != pendingSectorTileRequests.end() && loaded < MAX_SECTOR_TILE_LOADS_PER_FRAME)
        {
            auto& request = *it;
            auto tileIt = request.tileCoords.begin();

            while (tileIt != request.tileCoords.end() && loaded < MAX_SECTOR_TILE_LOADS_PER_FRAME)
            {
                const auto& coord = *tileIt;

                if (streamer.isTileLoaded(coord))
                {
                    tileIt = request.tileCoords.erase(tileIt);
                    continue;
                }

                navigation::NavmeshTileData tileData;
                if (tileCache->loadTile(coord, tileData))
                {
                    if (navmeshProvider->addNavmeshTile(tileData))
                    {
                        streamer.markTileLoaded(coord);
                        loaded++;

                        events::navmesh::NavmeshTileLoadedNotification notif;
                        notif.tileX = coord.x;
                        notif.tileZ = coord.z;
                        dispatcher.publish(notif);
                    }
                    tileIt = request.tileCoords.erase(tileIt);
                }
                else
                {
                    // Not in cache — queue for on-demand generation
                    pendingGenerationTiles.insert(coord);
                    tileIt = request.tileCoords.erase(tileIt);
                }
            }

            if (request.tileCoords.empty())
                it = pendingSectorTileRequests.erase(it);
            else
                ++it;
        }
    }

    void NavmeshTileManager::releaseTilesForSector(const world::SectorCoord& sectorCoord,
                                                     const glm::vec3& boundsMin,
                                                     const glm::vec3& boundsMax)
    {
        auto tiles = computeTilesForBounds(boundsMin, boundsMax);
        float tileWorldSize = bakeSettings.tileSize * bakeSettings.cellSize;

        auto& dispatcher = ::events::EventDispatcher::instance();

        for (const auto& coord : tiles)
        {
            auto refIt = sectorRefCounts.find(coord);
            if (refIt == sectorRefCounts.end())
                continue;

            refIt->second--;
            if (refIt->second <= 0)
            {
                sectorRefCounts.erase(refIt);

                // No sector needs this tile anymore — unload if also outside invoker ranges
                if (streamer.isTileLoaded(coord))
                {
                    bool withinInvoker = false;
                    float cx = (coord.x + 0.5f) * tileWorldSize;
                    float cz = (coord.z + 0.5f) * tileWorldSize;

                    for (const auto& src : invokerSources)
                    {
                        float dx = src.position.x - cx;
                        float dz = src.position.z - cz;
                        float distSq = dx * dx + dz * dz;
                        if (distSq <= src.unloadRadiusSq)
                        {
                            withinInvoker = true;
                            break;
                        }
                    }

                    if (!withinInvoker)
                    {
                        navmeshProvider->removeNavmeshTile(coord.x, coord.z);
                        streamer.markTileUnloaded(coord);

                        events::navmesh::NavmeshTileUnloadedNotification notif;
                        notif.tileX = coord.x;
                        notif.tileZ = coord.z;
                        dispatcher.publish(notif);
                    }
                }
            }
        }
    }
}
