#pragma once

#include "../../providers/navmesh/INavmeshProvider.hpp"
#include "../../data/EntityHandle.hpp"
#include "navigation/NavmeshData.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <functional>

namespace services
{
    class NavmeshAgentManager
    {
    public:
        using TileCoordFunc = std::function<navigation::NavmeshTileCoord(const glm::vec3&)>;

        NavmeshAgentManager(INavmeshProvider* provider, TileCoordFunc worldToTileCoord);

        void addAgent(EntityHandle entity);
        void removeAgent(EntityHandle entity);
        void setAgentDestination(EntityHandle entity, const glm::vec3& target);
        void stopAgent(EntityHandle entity);
        void updateAgentConfig(EntityHandle entity, float maxSpeed, float maxAcceleration);
        glm::vec3 getAgentVelocity(EntityHandle entity) const;
        void updatePositions(float deltaTime);

        void suspendAgentsOnUnloadedTiles(const std::vector<navigation::NavmeshTileCoord>& unloadedTiles);
        void resumeAgentsOnLoadedTiles(const std::vector<navigation::NavmeshTileCoord>& loadedTiles);

        void clear();

    private:
        INavmeshProvider* navmeshProvider;
        TileCoordFunc worldToTileCoord;

        std::unordered_map<uint64_t, int> entityToAgentIndex;
        std::unordered_map<uint64_t, glm::vec3> entityToTarget;
        std::unordered_map<uint64_t, float> entityStuckTimer;
        static constexpr float ARRIVAL_DISTANCE = 1.0f;
        static constexpr float STUCK_VELOCITY_THRESHOLD = 0.1f;
        static constexpr float STUCK_TIME_THRESHOLD = 0.5f;

        struct SuspendedAgent
        {
            uint64_t entityId;
            glm::vec3 position;
            glm::vec3 target;
            bool hasTarget;
        };
        std::vector<SuspendedAgent> suspendedAgents;
    };
}
