#pragma once

#include "../../services/providers/navmesh/INavmeshProvider.hpp"
#include "TileGraph.hpp"
#include <memory>
#include <mutex>

class dtNavMesh;
class dtNavMeshQuery;
class dtCrowd;
class dtQueryFilter;

namespace core
{
    class NavmeshAdapter : public services::INavmeshProvider
    {
    private:
        dtNavMesh* navMesh = nullptr;
        dtNavMeshQuery* navQuery = nullptr;
        dtCrowd* crowd = nullptr;
        bool initialized = false;

        mutable std::mutex navMeshMutex;
        mutable std::mutex progressMutex;
        types::NavmeshBakeProgress currentProgress;
        types::NavmeshBakeSettings storedSettings;
        TileGraph tileGraph;
        int maxResidentTiles = 0;
        int budgetWarnCount = 0;
    public:
        explicit NavmeshAdapter();
        ~NavmeshAdapter() override;

        NavmeshAdapter(const NavmeshAdapter&) = delete;
        NavmeshAdapter& operator=(const NavmeshAdapter&) = delete;

        bool init() override;
        void cleanUp() override;
        bool isInitialized() const override;

        // === Navmesh Building ===
        bool buildNavmesh(const navigation::NavmeshInputGeometry& geometry,
                           const types::NavmeshBakeSettings& settings,
                           const navigation::OffMeshConnectionsMap& tileOffMeshLinks = {},
                           const navigation::AreaModifiersMap& tileAreaModifiers = {}) override;
        types::NavmeshBakeProgress getBuildProgress() const override;

        // === Tiled Navmesh ===
        bool initTiledNavmesh(const types::NavmeshBakeSettings& settings,
                               const glm::vec3& boundsMin, const glm::vec3& boundsMax) override;
        navigation::NavmeshTileData buildSingleTile(int tx, int tz,
                                                      const navigation::NavmeshInputGeometry& geometry,
                                                      const types::NavmeshBakeSettings& settings,
                                                      const navigation::NavmeshOffMeshConnections& offMeshLinks = {},
                                                      const std::vector<navigation::NavmeshAreaModifier>& areaModifiers = {},
                                                      uint8_t lod = 0) override;
        bool addNavmeshTile(const navigation::NavmeshTileData& tileData) override;
        bool removeNavmeshTile(int tx, int tz) override;
        int getMaxResidentTiles() const override { return maxResidentTiles; }

        // === Serialization ===
        std::vector<navigation::NavmeshTileData> serializeNavmesh() const override;
        bool deserializeNavmesh(const navigation::NavmeshFileHeader& header,
                                 const std::vector<navigation::NavmeshTileData>& tiles) override;
        bool hasNavmesh() const override;
        void clearNavmesh() override;

        // === Pathfinding ===
        navigation::NavPath findPath(const glm::vec3& start, const glm::vec3& end,
                                      float agentRadius, float agentHeight) override;
        navigation::NavmeshRaycastResult navmeshRaycast(const glm::vec3& from, const glm::vec3& to) override;
        glm::vec3 getClosestPoint(const glm::vec3& point, float searchRadius) override;
        bool isPointOnNavmesh(const glm::vec3& point, float tolerance) override;

        // === Crowd / Agent ===
        int addCrowdAgent(const glm::vec3& position, float radius, float height,
                           float maxSpeed, float maxAcceleration) override;
        void removeCrowdAgent(int agentIndex) override;
        void setCrowdAgentTarget(int agentIndex, const glm::vec3& target) override;
        void stopCrowdAgent(int agentIndex) override;
        void updateCrowdAgentParams(int agentIndex, float maxSpeed, float maxAcceleration) override;
        glm::vec3 getCrowdAgentPosition(int agentIndex) const override;
        glm::vec3 getCrowdAgentVelocity(int agentIndex) const override;
        float getCrowdAgentMaxSpeed(int agentIndex) const override;
        void updateCrowd(float deltaTime) override;
        bool overrideCrowdAgentVelocity(int agentIndex, const glm::vec3& velocity) override;

        // === Crowd Filters ===
        void configureCrowdFilter(int filterIndex, const float* areaCosts, int numAreas) override;
        void setCrowdAgentFilterType(int agentIndex, uint8_t filterType) override;

        // === Tile Graph (Hierarchical Pathfinding) ===
        void onTileAdded(int tx, int tz) override;
        void onTileRemoved(int tx, int tz) override;

        // === Debug ===
        void getDebugMesh(std::vector<glm::vec3>& outVertices,
                           std::vector<uint32_t>& outIndices) const override;

    private:
        void destroyNavMesh();
        void destroyNavMeshLocked();
        void initCrowd(float agentRadius);
        bool initializeNavmesh(unsigned char* navData, int navDataSize, float agentRadius);
        void updateProgress(types::NavmeshBakeStatus status, float progress, const char* stage);
        void configureQueryFilter(dtQueryFilter* filter) const;
        navigation::NavPath findPathHierarchical(const glm::vec3& start, const glm::vec3& end,
                                                  float agentRadius, float agentHeight,
                                                  const dtQueryFilter& filter);
        navigation::NavmeshTileCoord worldToTileCoord(const glm::vec3& pos) const;
    };
}
