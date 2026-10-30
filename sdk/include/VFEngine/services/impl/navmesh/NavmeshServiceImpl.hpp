#pragma once

#include "../../interfaces/navmesh/INavmeshService.hpp"
#include "../../providers/navmesh/INavmeshProvider.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/navmesh/NavmeshAuthoringEvents.hpp"
#include "../../events/EventTypes.hpp"
#include "NavmeshTileManager.hpp"
#include "NavmeshAgentManager.hpp"
#include "NavmeshWorldBaker.hpp"
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

namespace services
{
    class NavmeshServiceImpl : public INavmeshService
    {
    private:
        INavmeshProvider* navmeshProvider;
        types::NavmeshBakeSettings lastBakeSettings;
        std::future<bool> bakeFuture;

        // VK-1660 ticketed bakes (RequestNavmeshBakeCommand). runningBakeTicket is the ticket of the bake
        // bakeFuture tracks (0 for a BakeNavmeshCommand bake); bakeOutcomes keeps the most recent finished
        // ones, oldest first.
        uint64_t nextBakeTicket = 1;
        uint64_t runningBakeTicket = 0;
        std::deque<::events::navmesh::NavmeshBakeOutcome> bakeOutcomes;
        static constexpr size_t MAX_BAKE_OUTCOMES = 8;
        // A bake finished since the navmesh was last saved, loaded or cleared.
        bool unsavedBake = false;

        NavmeshTileManager tileManager;
        NavmeshAgentManager agentManager;
        std::unique_ptr<NavmeshWorldBaker> worldBaker;
        bool playModeActive = false;
        // The debug-overlay toggle (GetShowNavmeshDebugQuery) has an editor-only handler; once a query
        // finds none (the Runtime), the overlay stays off instead of throwing every frame.
        bool debugToggleMissing = false;

        // Bumped on every tile load/unload/update — scripts poll this to detect
        // stale cached paths (see GetNavmeshTileVersionQuery)
        uint64_t tileVersion = 0;

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
        void update(float deltaTime, bool simulateAgents) override;

        void getNavmeshDebugMesh(std::vector<glm::vec3>& outVertices,
                                 std::vector<uint32_t>& outIndices) const override;

        // === Tool support (VK-1660, events in NavmeshAuthoringEvents.hpp) ===
        ::events::navmesh::NavmeshBakeTicket requestBake(const types::NavmeshBakeSettings& settings,
                                                         const std::optional<::events::navmesh::NavmeshBakeBounds>& bounds);
        ::events::navmesh::NavmeshBakeOutcome getBakeOutcome(uint64_t ticket) const;
        ::events::navmesh::NavmeshStatus getStatus();

    private:
        // Starts an asynchronous scene bake (ticket 0 = untracked); returns why it did not start ("" = started).
        std::string startSceneBake(const types::NavmeshBakeSettings& settings,
                                   const std::optional<::events::navmesh::NavmeshBakeBounds>& bounds,
                                   uint64_t ticket);
        void pollBakeCompletion();
        // Records the running bake's outcome (when it has a ticket) and publishes NavmeshBakeCompleteNotification.
        void finishSceneBake(bool success, const std::string& message);
        // A navmesh clear or load replaces whatever a running scene bake builds: wait for it, report it failed.
        void abandonRunningBake();
        // Every load and clear re-creates (or destroys) the detour crowd: drop the crowd agents first so no
        // stale crowd index is read back as a position. Agents re-register on their next destination. (A
        // bake uses agentManager.beginCrowdRebuild instead, which also keeps the moves in progress.)
        void resetCrowdBookkeeping();
        bool navmeshDebugVisible();
        void attachNavmeshAssetToSceneRoot(const std::string& assetPath);
        std::string sceneNavmeshAssetPath() const;

        navigation::OffMeshConnectionsMap collectAllOffMeshLinks(const types::NavmeshBakeSettings& settings);
        navigation::NavmeshOffMeshConnections collectOffMeshLinksForTile(const navigation::NavmeshTileBounds& bounds,
                                                                         const types::NavmeshBakeSettings& settings);
        void drawOffMeshLinkDebug();
        void trackOffMeshLinkTransforms();
        void gatherInvokerSources();
        void drawInvokerDebug();
        void autoGenerateDropLinks(const types::NavmeshBakeSettings& settings,
                                    navigation::OffMeshConnectionsMap& outMap);

        // Obstacle support
        void trackObstacleTransforms();
        void drawObstacleDebug();
        void cleanupPhantomAgents();
        void setObstacleActive(EntityHandle entity, bool active);

        // Area modifier volume support
        navigation::AreaModifiersMap collectAllAreaModifiers(const types::NavmeshBakeSettings& settings);
        std::vector<navigation::NavmeshAreaModifier> collectAreaModifiersForTile(const navigation::NavmeshTileBounds& bounds);
        void trackModifierVolumeTransforms();
        void drawModifierVolumeDebug();

        // Patrol routes (VK-1660): each NavRouteComponent's ordered children, while navmesh debug drawing is on
        void drawRouteDebug();

        // Track entity positions for dirty tile marking
        std::unordered_map<uint64_t, glm::vec3> lastOffMeshLinkPositions;
        std::unordered_map<uint64_t, glm::vec3> lastObstaclePositions;
        std::unordered_map<uint64_t, glm::vec3> lastModifierVolumePositions;
    };
}
