#pragma once

#include "../../interfaces/navmesh/INavmeshService.hpp"
#include "../../providers/navmesh/INavmeshProvider.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/EventTypes.hpp"
#include "NavmeshStreamer.hpp"
#include "navigation/NavmeshTileCache.hpp"
#include <future>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace services
{
    class NavmeshServiceImpl : public INavmeshService
    {
    private:
        INavmeshProvider* navmeshProvider;
        types::NavmeshBakeSettings lastBakeSettings;
        std::future<bool> bakeFuture;

        std::unordered_map<uint64_t, int> entityToAgentIndex;

        // Streaming (VK-777)
        NavmeshStreamer streamer;
        std::unique_ptr<navigation::NavmeshTileCache> tileCache;
        glm::vec3 lastCameraPos{0.0f};

        // Dirty tile tracking (VK-778)
        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> dirtyTiles;
        struct PendingTileBake
        {
            navigation::NavmeshTileCoord coord;
            std::future<navigation::NavmeshTileData> future;
        };
        std::vector<PendingTileBake> pendingTileBakes;
        static constexpr int MAX_TILE_BAKES_PER_FRAME = 2;

        // Suspended agents (VK-779)
        struct SuspendedAgent
        {
            uint64_t entityId;
            glm::vec3 position;
            glm::vec3 target;
            bool hasTarget;
        };
        std::vector<SuspendedAgent> suspendedAgents;

        // Event tokens
        ::events::SubscriptionToken brushAppliedToken;
        ::events::SubscriptionToken holeBrushAppliedToken;
        ::events::SubscriptionToken tileAddedToken;
        ::events::SubscriptionToken tileRemovedToken;

    public:
        explicit NavmeshServiceImpl(INavmeshProvider* navmeshProvider);
        ~NavmeshServiceImpl() override;

        void registerEventHandlers() override;

        void bakeNavmesh(const types::NavmeshBakeSettings& settings) override;
        types::NavmeshBakeProgress getBakeProgress() const override;

        bool saveNavmesh(const std::string& filePath) override;
        bool loadNavmesh(const std::string& filePath) override;
        bool hasNavmesh() const override;
        void clearNavmesh() override;

        // Per-tile operations (VK-739)
        bool bakeSingleTile(int tileX, int tileZ) override;
        bool saveNavmeshTiled(const std::string& directory) override;
        bool loadNavmeshTiled(const std::string& directory) override;

        navigation::NavPath findPath(const glm::vec3& start, const glm::vec3& end,
                                     float agentRadius = 0.3f, float agentHeight = 2.0f) override;
        glm::vec3 getClosestPointOnNavmesh(const glm::vec3& point, float searchRadius = 5.0f) override;
        bool isPointOnNavmesh(const glm::vec3& point, float tolerance = 0.5f) override;

        void addAgent(EntityHandle entity) override;
        void removeAgent(EntityHandle entity) override;
        void setAgentDestination(EntityHandle entity, const glm::vec3& target) override;
        void stopAgent(EntityHandle entity) override;
        void updateAgents(float deltaTime) override;

        void getNavmeshDebugMesh(std::vector<glm::vec3>& outVertices,
                                 std::vector<uint32_t>& outIndices) const override;

    private:
        void pollBakeCompletion();
        void pollTileBakeCompletions();
        void updateStreaming();
        void processDirtyTiles();
        void suspendAgentsOnUnloadedTiles(const std::vector<navigation::NavmeshTileCoord>& unloadedTiles);
        void resumeAgentsOnLoadedTiles(const std::vector<navigation::NavmeshTileCoord>& loadedTiles);
        void markTileDirty(int tileX, int tileZ);
        navigation::NavmeshTileCoord worldToTileCoord(const glm::vec3& worldPos) const;

        void collectSceneGeometry(const types::NavmeshBakeSettings& settings,
                                  navigation::NavmeshInputGeometry& outGeometry);
        void collectTileGeometry(const navigation::NavmeshTileBounds& bounds,
                                 const types::NavmeshBakeSettings& settings,
                                 navigation::NavmeshInputGeometry& outGeometry);
        void collectTerrainGeometry(navigation::NavmeshInputGeometry& outGeometry);
        void collectTerrainGeometryForBounds(const navigation::NavmeshTileBounds& bounds,
                                              navigation::NavmeshInputGeometry& outGeometry);
        void collectStaticMeshGeometry(navigation::NavmeshInputGeometry& outGeometry);
        void collectStaticMeshGeometryForBounds(const navigation::NavmeshTileBounds& bounds,
                                                 navigation::NavmeshInputGeometry& outGeometry);
        void collectColliderGeometry(navigation::NavmeshInputGeometry& outGeometry);
        void collectColliderGeometryForBounds(const navigation::NavmeshTileBounds& bounds,
                                               navigation::NavmeshInputGeometry& outGeometry);
    };
}
