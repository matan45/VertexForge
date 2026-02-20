#pragma once
#include "../data/EntityHandle.hpp"
#include "navigation/NavmeshData.hpp"
#include "types/NavmeshTypes.hpp"
#include <glm/glm.hpp>
#include <string>

namespace services
{
    class INavmeshService
    {
    public:
        virtual ~INavmeshService() = default;

        virtual void registerEventHandlers() = 0;

        // === Baking ===
        virtual void bakeNavmesh(const types::NavmeshBakeSettings& settings) = 0;
        virtual types::NavmeshBakeProgress getBakeProgress() const = 0;

        // === Serialization ===
        virtual bool saveNavmesh(const std::string& filePath) = 0;
        virtual bool loadNavmesh(const std::string& filePath) = 0;
        virtual bool hasNavmesh() const = 0;
        virtual void clearNavmesh() = 0;

        // === Pathfinding ===
        virtual navigation::NavPath findPath(const glm::vec3& start, const glm::vec3& end,
                                              float agentRadius = 0.3f, float agentHeight = 2.0f) = 0;
        virtual glm::vec3 getClosestPointOnNavmesh(const glm::vec3& point, float searchRadius = 5.0f) = 0;
        virtual bool isPointOnNavmesh(const glm::vec3& point, float tolerance = 0.5f) = 0;

        // === Agent Management ===
        virtual void addAgent(EntityHandle entity) = 0;
        virtual void removeAgent(EntityHandle entity) = 0;
        virtual void setAgentDestination(EntityHandle entity, const glm::vec3& target) = 0;
        virtual void stopAgent(EntityHandle entity) = 0;
        virtual void updateAgents(float deltaTime) = 0;

        // === Debug ===
        virtual void getNavmeshDebugMesh(std::vector<glm::vec3>& outVertices,
                                          std::vector<uint32_t>& outIndices) const = 0;
    };
}
