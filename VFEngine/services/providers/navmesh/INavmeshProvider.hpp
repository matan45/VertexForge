#pragma once
#include "navigation/NavmeshData.hpp"
#include "types/NavmeshTypes.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace services
{
    class INavmeshProvider
    {
    public:
        virtual ~INavmeshProvider() = default;

        virtual bool init() = 0;
        virtual void cleanUp() = 0;
        virtual bool isInitialized() const = 0;

        virtual bool buildNavmesh(const navigation::NavmeshInputGeometry& geometry,
                                   const types::NavmeshBakeSettings& settings,
                                   const navigation::OffMeshConnectionsMap& tileOffMeshLinks = {},
                                   const navigation::AreaModifiersMap& tileAreaModifiers = {}) = 0;
        virtual types::NavmeshBakeProgress getBuildProgress() const = 0;

        // === Tiled Navmesh ===
        virtual bool initTiledNavmesh(const types::NavmeshBakeSettings& settings,
                                       const glm::vec3& boundsMin, const glm::vec3& boundsMax) = 0;
        virtual navigation::NavmeshTileData buildSingleTile(int tx, int tz,
                                                              const navigation::NavmeshInputGeometry& geometry,
                                                              const types::NavmeshBakeSettings& settings,
                                                              const navigation::NavmeshOffMeshConnections& offMeshLinks = {},
                                                              const std::vector<navigation::NavmeshAreaModifier>& areaModifiers = {}) = 0;
        virtual bool addNavmeshTile(const navigation::NavmeshTileData& tileData) = 0;
        virtual bool removeNavmeshTile(int tx, int tz) = 0;

        virtual std::vector<navigation::NavmeshTileData> serializeNavmesh() const = 0;
        virtual bool deserializeNavmesh(const navigation::NavmeshFileHeader& header,
                                         const std::vector<navigation::NavmeshTileData>& tiles) = 0;
        virtual bool hasNavmesh() const = 0;
        virtual void clearNavmesh() = 0;

        virtual navigation::NavPath findPath(const glm::vec3& start, const glm::vec3& end,
                                              float agentRadius, float agentHeight) = 0;
        virtual navigation::NavmeshRaycastResult navmeshRaycast(const glm::vec3& from, const glm::vec3& to) = 0;
        virtual glm::vec3 getClosestPoint(const glm::vec3& point, float searchRadius) = 0;
        virtual bool isPointOnNavmesh(const glm::vec3& point, float tolerance) = 0;

        virtual int addCrowdAgent(const glm::vec3& position, float radius, float height,
                                   float maxSpeed, float maxAcceleration) = 0;
        virtual void removeCrowdAgent(int agentIndex) = 0;
        virtual void setCrowdAgentTarget(int agentIndex, const glm::vec3& target) = 0;
        virtual void stopCrowdAgent(int agentIndex) = 0;
        virtual void updateCrowdAgentParams(int agentIndex, float maxSpeed, float maxAcceleration) = 0;
        virtual glm::vec3 getCrowdAgentPosition(int agentIndex) const = 0;
        virtual glm::vec3 getCrowdAgentVelocity(int agentIndex) const = 0;
        virtual float getCrowdAgentMaxSpeed(int agentIndex) const = 0;
        virtual void updateCrowd(float deltaTime) = 0;

        virtual void configureCrowdFilter(int filterIndex, const float* areaCosts, int numAreas) = 0;
        virtual void setCrowdAgentFilterType(int agentIndex, uint8_t filterType) = 0;

        // Tile graph updates (hierarchical pathfinding)
        virtual void onTileAdded(int tx, int tz) = 0;
        virtual void onTileRemoved(int tx, int tz) = 0;

        virtual void getDebugMesh(std::vector<glm::vec3>& outVertices,
                                   std::vector<uint32_t>& outIndices) const = 0;
    };
}
