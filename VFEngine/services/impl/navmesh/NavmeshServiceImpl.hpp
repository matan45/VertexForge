#pragma once

#include "../../interfaces/navmesh/INavmeshService.hpp"
#include "../../providers/navmesh/INavmeshProvider.hpp"
#include <future>
#include <unordered_map>

namespace services
{
    class NavmeshServiceImpl : public INavmeshService
    {
    private:
        INavmeshProvider* navmeshProvider;
        types::NavmeshBakeSettings lastBakeSettings;
        std::future<bool> bakeFuture;

        std::unordered_map<uint64_t, int> entityToAgentIndex;

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
        void collectSceneGeometry(const types::NavmeshBakeSettings& settings,
                                  navigation::NavmeshInputGeometry& outGeometry);
        void collectTerrainGeometry(navigation::NavmeshInputGeometry& outGeometry);
        void collectStaticMeshGeometry(navigation::NavmeshInputGeometry& outGeometry);
        void collectColliderGeometry(navigation::NavmeshInputGeometry& outGeometry);
    };
}
