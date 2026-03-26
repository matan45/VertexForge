#pragma once

#include "NavmeshStreamer.hpp"
#include "../../providers/navmesh/INavmeshProvider.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/EventTypes.hpp"
#include "navigation/NavmeshTileCache.hpp"
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>
#include <future>
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

        void markTileDirty(int tileX, int tileZ);
        void processDirtyTiles();
        void pollTileBakeCompletions();
        StreamingResult updateStreaming();

        navigation::NavmeshTileCoord worldToTileCoord(const glm::vec3& worldPos) const;

        void clear();

        // Accessors for event handler delegation
        NavmeshStreamer& getStreamer() { return streamer; }
        navigation::NavmeshTileCache* getTileCache() const { return tileCache.get(); }
        const std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash>& getDirtyTiles() const { return dirtyTiles; }

        void setLastCameraPos(const glm::vec3& pos) { lastCameraPos = pos; }

        // Event registration for brush subscriptions
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

        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> dirtyTiles;

        struct PendingTileBake
        {
            navigation::NavmeshTileCoord coord;
            std::future<navigation::NavmeshTileData> future;
        };
        std::vector<PendingTileBake> pendingTileBakes;
        static constexpr int MAX_TILE_BAKES_PER_FRAME = 2;

        ::events::SubscriptionToken brushAppliedToken;
        ::events::SubscriptionToken holeBrushAppliedToken;
    };
}
