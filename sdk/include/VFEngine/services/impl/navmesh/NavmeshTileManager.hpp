#pragma once

#include "NavmeshStreamer.hpp"
#include "../../providers/navmesh/INavmeshProvider.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/EventTypes.hpp"
#include "navigation/NavmeshTileCache.hpp"
#include "world/WorldTypes.hpp"
#include <glm/glm.hpp>
#include "threading/JobSystem.hpp"
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>

namespace services
{
    struct StreamingResult
    {
        std::vector<navigation::NavmeshTileCoord> loaded;
        std::vector<navigation::NavmeshTileCoord> unloaded;
    };

    class NavmeshTileManager
    {
    public:
        using CollectGeometryFunc = std::function<void(const navigation::NavmeshTileBounds&,
                                                        const types::NavmeshBakeSettings&,
                                                        navigation::NavmeshInputGeometry&)>;
        using CollectOffMeshFunc = std::function<navigation::NavmeshOffMeshConnections(
                                                        const navigation::NavmeshTileBounds&,
                                                        const types::NavmeshBakeSettings&)>;
        using CollectAreaModifiersFunc = std::function<std::vector<navigation::NavmeshAreaModifier>(
                                                        const navigation::NavmeshTileBounds&)>;

        NavmeshTileManager(INavmeshProvider* provider, const types::NavmeshBakeSettings& settings,
                          CollectGeometryFunc collectTileGeometry);

        void setOffMeshLinkCollector(CollectOffMeshFunc func) { collectOffMeshLinks = std::move(func); }
        void setAreaModifierCollector(CollectAreaModifiersFunc func) { collectAreaModifiers = std::move(func); }

        bool bakeSingleTile(int tileX, int tileZ);
        bool saveNavmeshTiled(const std::string& directory);
        bool loadNavmeshTiled(const std::string& directory, types::NavmeshBakeSettings& outSettings);

        // Loads every cached tile not currently resident (streaming off / "Load All Tiles")
        int loadAllTilesFromCache();

        // === Whole-world bake support (driven by NavmeshWorldBaker) ===

        // Points the tile cache at a fresh output directory without loading anything
        void prepareTileCache(const std::string& directory);

        // Collects geometry for one tile on the calling thread and submits the bake
        // async (saved to cache on completion). Returns false when the tile has no
        // geometry (skipped — nothing to bake).
        bool submitWorldBakeTile(const navigation::NavmeshTileCoord& coord);

        int getPendingBakeCount() const { return static_cast<int>(pendingTileBakes.size()); }

        // Writes index.vfNavIndex from everything currently in the tile cache
        bool finalizeWorldBakeIndex();

        void markTileDirty(int tileX, int tileZ);
        void processDirtyTiles();
        void processOnDemandGeneration();
        void pollTileBakeCompletions();
        StreamingResult updateStreaming();

        void setInvokerSources(std::vector<StreamingSource> sources);
        void setSaveOnDemandToCache(bool save) { saveOnDemandToCache = save; }
        void ensureTiledNavmeshInitialized();

        navigation::NavmeshTileCoord worldToTileCoord(const glm::vec3& worldPos) const;

        void clear();

        // Accessors for event handler delegation
        NavmeshStreamer& getStreamer() { return streamer; }
        navigation::NavmeshTileCache* getTileCache() const { return tileCache.get(); }
        const std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash>& getDirtyTiles() const { return dirtyTiles; }

        void setLastCameraPos(const glm::vec3& pos) { lastCameraPos = pos; }

        // Sector-navmesh coordination
        void prioritizeTilesForBounds(const world::SectorCoord& sectorCoord,
                                       const glm::vec3& boundsMin, const glm::vec3& boundsMax);
        void releaseTilesForSector(const world::SectorCoord& sectorCoord,
                                    const glm::vec3& boundsMin, const glm::vec3& boundsMax);
        std::vector<navigation::NavmeshTileCoord> computeTilesForBounds(
            const glm::vec3& boundsMin, const glm::vec3& boundsMax) const;

        // Event registration for brush and sector subscriptions
        void registerEvents();
        void unregisterEvents();

    private:
        INavmeshProvider* navmeshProvider;
        const types::NavmeshBakeSettings& bakeSettings;
        CollectGeometryFunc collectTileGeometry;
        CollectOffMeshFunc collectOffMeshLinks;
        CollectAreaModifiersFunc collectAreaModifiers;

        NavmeshStreamer streamer;
        std::unique_ptr<navigation::NavmeshTileCache> tileCache;
        glm::vec3 lastCameraPos{0.0f};

        std::vector<StreamingSource> invokerSources;
        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> dirtyTiles;
        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> pendingGenerationTiles;
        bool tiledNavmeshInitialized = false;
        bool saveOnDemandToCache = true;

        // In-flight bake jobs. The handle is used only for throttling (count) and for
        // draining on clear(); the baked result is delivered out-of-band via completedBakes
        // so completion runs on the main thread without per-future polling.
        struct PendingTileBake
        {
            navigation::NavmeshTileCoord coord;
            threading::JobHandle handle;
        };
        std::vector<PendingTileBake> pendingTileBakes;
        static constexpr int MAX_TILE_BAKES_PER_FRAME = 2;

        // Results pushed by bake worker threads, drained + applied on the main thread in
        // pollTileBakeCompletions(). Replaces the old wait_for(0) polling loop.
        struct CompletedTileBake
        {
            navigation::NavmeshTileCoord coord;
            bool saveToCache = true;
            navigation::NavmeshTileData tileData;
        };
        std::mutex completedBakesMutex;
        std::vector<CompletedTileBake> completedBakes;

        void submitTileBake(const navigation::NavmeshTileCoord& coord,
                            navigation::NavmeshInputGeometry geometry,
                            navigation::NavmeshOffMeshConnections offMeshLinks,
                            std::vector<navigation::NavmeshAreaModifier> areaModifiers,
                            bool saveToCache, threading::JobPriority priority);

        int initialLoadBurst = 0;
        static constexpr int INITIAL_STREAM_LOAD_BURST = 64;

        ::events::SubscriptionToken brushAppliedToken;
        ::events::SubscriptionToken holeBrushAppliedToken;

        // Sector-navmesh coordination
        struct SectorTileRequest
        {
            world::SectorCoord sectorCoord;
            std::vector<navigation::NavmeshTileCoord> tileCoords;
        };

        std::vector<SectorTileRequest> pendingSectorTileRequests;
        std::unordered_map<navigation::NavmeshTileCoord, int, navigation::NavmeshTileCoordHash> sectorRefCounts;

        ::events::SubscriptionToken sectorAboutToLoadToken;
        ::events::SubscriptionToken sectorUnloadedToken;

        void processSectorTileRequests();
        static constexpr int MAX_SECTOR_TILE_LOADS_PER_FRAME = 4;
    };
}
