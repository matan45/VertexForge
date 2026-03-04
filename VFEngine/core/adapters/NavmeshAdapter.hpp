#pragma once

#include "../../services/providers/navmesh/INavmeshProvider.hpp"
#include <memory>
#include <mutex>

class dtNavMesh;
class dtNavMeshQuery;
class dtCrowd;

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
                           const types::NavmeshBakeSettings& settings) override;
        types::NavmeshBakeProgress getBuildProgress() const override;

        // === Serialization ===
        std::vector<navigation::NavmeshTileData> serializeNavmesh() const override;
        bool deserializeNavmesh(const navigation::NavmeshFileHeader& header,
                                 const std::vector<navigation::NavmeshTileData>& tiles) override;
        bool hasNavmesh() const override;
        void clearNavmesh() override;

        // === Pathfinding ===
        navigation::NavPath findPath(const glm::vec3& start, const glm::vec3& end,
                                      float agentRadius, float agentHeight) override;
        glm::vec3 getClosestPoint(const glm::vec3& point, float searchRadius) override;
        bool isPointOnNavmesh(const glm::vec3& point, float tolerance) override;

        // === Crowd / Agent ===
        int addCrowdAgent(const glm::vec3& position, float radius, float height,
                           float maxSpeed, float maxAcceleration) override;
        void removeCrowdAgent(int agentIndex) override;
        void setCrowdAgentTarget(int agentIndex, const glm::vec3& target) override;
        void stopCrowdAgent(int agentIndex) override;
        glm::vec3 getCrowdAgentPosition(int agentIndex) const override;
        glm::vec3 getCrowdAgentVelocity(int agentIndex) const override;
        void updateCrowd(float deltaTime) override;

        // === Debug ===
        void getDebugMesh(std::vector<glm::vec3>& outVertices,
                           std::vector<uint32_t>& outIndices) const override;

    private:
        void destroyNavMesh();
        void destroyNavMeshLocked();
        void initCrowd(float agentRadius);
        bool initializeNavmesh(unsigned char* navData, int navDataSize, float agentRadius);
        void updateProgress(types::NavmeshBakeStatus status, float progress, const char* stage);
    };
}
