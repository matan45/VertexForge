#include "NavmeshAgentManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <unordered_set>

namespace services
{
    NavmeshAgentManager::NavmeshAgentManager(INavmeshProvider* provider, TileCoordFunc worldToTileCoordFunc)
        : navmeshProvider(provider), worldToTileCoord(std::move(worldToTileCoordFunc))
    {
    }

    void NavmeshAgentManager::addAgent(EntityHandle entity)
    {
        if (entityToAgentIndex.count(entity.id))
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = internal::fromHandle(entity);
        if (!registry.valid(enttEntity))
        {
            return;
        }

        float radius = 0.3f;
        float height = 2.0f;
        float maxSpeed = 3.5f;
        float maxAcceleration = 8.0f;

        if (registry.all_of<components::NavmeshAgentComponent>(enttEntity))
        {
            const auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
            radius = agent.radius;
            height = agent.height;
            maxSpeed = agent.maxSpeed;
            maxAcceleration = agent.maxAcceleration;
        }

        glm::vec3 position{0.0f};
        if (registry.all_of<components::TransformComponent>(enttEntity))
        {
            position = registry.get<components::TransformComponent>(enttEntity).position;
        }

        int agentIdx = navmeshProvider->addCrowdAgent(position, radius, height, maxSpeed, maxAcceleration);
        if (agentIdx >= 0)
        {
            entityToAgentIndex[entity.id] = agentIdx;

            if (registry.all_of<components::NavmeshAgentComponent>(enttEntity))
            {
                auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
                agent.isActive = true;
                agent.crowdAgentIndex = agentIdx;
            }
        }
    }

    void NavmeshAgentManager::removeAgent(EntityHandle entity)
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it != entityToAgentIndex.end())
        {
            navmeshProvider->removeCrowdAgent(it->second);

            auto& registry = scene::EntityRegistry::getRegistry();
            auto enttEntity = internal::fromHandle(entity);
            if (registry.valid(enttEntity) && registry.all_of<components::NavmeshAgentComponent>(enttEntity))
            {
                auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
                agent.isActive = false;
                agent.crowdAgentIndex = -1;
            }

            entityToAgentIndex.erase(it);
        }
    }

    void NavmeshAgentManager::setAgentDestination(EntityHandle entity, const glm::vec3& target)
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it != entityToAgentIndex.end())
        {
            navmeshProvider->setCrowdAgentTarget(it->second, target);
        }
    }

    void NavmeshAgentManager::stopAgent(EntityHandle entity)
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it != entityToAgentIndex.end())
        {
            navmeshProvider->stopCrowdAgent(it->second);
        }
    }

    void NavmeshAgentManager::updatePositions(float deltaTime)
    {
        if (entityToAgentIndex.empty())
        {
            return;
        }

        navmeshProvider->updateCrowd(deltaTime);

        auto& registry = scene::EntityRegistry::getRegistry();
        for (const auto& [entityId, agentIdx] : entityToAgentIndex)
        {
            EntityHandle handle{entityId};
            auto enttEntity = internal::fromHandle(handle);
            if (!registry.valid(enttEntity) || !registry.all_of<components::TransformComponent>(enttEntity))
            {
                continue;
            }

            glm::vec3 agentPos = navmeshProvider->getCrowdAgentPosition(agentIdx);
            auto& transform = registry.get<components::TransformComponent>(enttEntity);
            transform.position = agentPos;
            transform.isDirty = true;
        }
    }

    void NavmeshAgentManager::suspendAgentsOnUnloadedTiles(
        const std::vector<navigation::NavmeshTileCoord>& unloadedTiles)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> unloadedSet(
            unloadedTiles.begin(), unloadedTiles.end());

        auto toRemove = std::vector<uint64_t>();

        for (const auto& [entityId, agentIdx] : entityToAgentIndex)
        {
            glm::vec3 agentPos = navmeshProvider->getCrowdAgentPosition(agentIdx);
            auto tileCoord = worldToTileCoord(agentPos);

            if (unloadedSet.count(tileCoord))
            {
                SuspendedAgent suspended;
                suspended.entityId = entityId;
                suspended.position = agentPos;
                suspended.target = glm::vec3(0.0f);
                suspended.hasTarget = false;

                auto enttEntity = internal::fromHandle(EntityHandle{entityId});
                if (registry.valid(enttEntity) && registry.all_of<components::NavmeshAgentComponent>(enttEntity))
                {
                    auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
                    agent.isSuspended = true;
                    agent.suspendedPosition = agentPos;
                }

                navmeshProvider->removeCrowdAgent(agentIdx);
                suspendedAgents.push_back(suspended);
                toRemove.push_back(entityId);
            }
        }

        for (uint64_t id : toRemove)
        {
            entityToAgentIndex.erase(id);
        }
    }

    void NavmeshAgentManager::resumeAgentsOnLoadedTiles(
        const std::vector<navigation::NavmeshTileCoord>& loadedTiles)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> loadedSet(
            loadedTiles.begin(), loadedTiles.end());

        auto it = suspendedAgents.begin();
        while (it != suspendedAgents.end())
        {
            auto tileCoord = worldToTileCoord(it->position);
            if (loadedSet.count(tileCoord))
            {
                auto enttEntity = internal::fromHandle(EntityHandle{it->entityId});
                if (registry.valid(enttEntity) && registry.all_of<components::NavmeshAgentComponent>(enttEntity))
                {
                    auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);

                    int agentIdx = navmeshProvider->addCrowdAgent(
                        it->position, agent.radius, agent.height, agent.maxSpeed, agent.maxAcceleration);

                    if (agentIdx >= 0)
                    {
                        entityToAgentIndex[it->entityId] = agentIdx;
                        agent.isActive = true;
                        agent.isSuspended = false;
                        agent.crowdAgentIndex = agentIdx;

                        if (it->hasTarget)
                        {
                            navmeshProvider->setCrowdAgentTarget(agentIdx, it->target);
                        }
                    }
                }

                it = suspendedAgents.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void NavmeshAgentManager::clear()
    {
        entityToAgentIndex.clear();
        suspendedAgents.clear();
    }
}
