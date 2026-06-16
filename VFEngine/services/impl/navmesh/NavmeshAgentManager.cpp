#include "NavmeshAgentManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
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
            velocityTransitionFrames.erase(entity.id);
        }
    }

    void NavmeshAgentManager::setAgentDestination(EntityHandle entity, const glm::vec3& target)
    {
        // Snap the requested destination onto the navmesh. A raw off-mesh point would
        // otherwise leave the crowd agent with no valid target (setCrowdAgentTarget's
        // findNearestPoly finds nothing) and trip false stuck-detection in updatePositions.
        // getClosestPoint returns the input unchanged when nothing is found, so each
        // candidate is validated with isPointOnNavmesh and the search widens on failure.
        glm::vec3 snapped = target;
        if (!navmeshProvider->isPointOnNavmesh(target, SNAP_ON_MESH_TOLERANCE))
        {
            bool resolved = false;
            for (float r : SNAP_SEARCH_RADII)
            {
                glm::vec3 candidate = navmeshProvider->getClosestPoint(target, r);
                if (navmeshProvider->isPointOnNavmesh(candidate, SNAP_ON_MESH_TOLERANCE))
                {
                    snapped = candidate;
                    resolved = true;
                    break;
                }
            }
            if (!resolved)
            {
                // Genuinely unreachable: clear any prior target so the agent idles
                // cleanly instead of accumulating a false "blocked" stuck timer.
                stopAgent(entity);
                return;
            }
        }

        auto it = entityToAgentIndex.find(entity.id);
        if (it == entityToAgentIndex.end())
        {
            // Nothing in the engine publishes AddAgentCommand, so an entity that
            // gained a NavmeshAgentComponent (at scene load, via a prefab, or via
            // a runtime addComponent) is not yet in the crowd. Register it lazily
            // on its first destination request -- by now the navmesh is loaded and
            // the entity is positioned, which is exactly when addCrowdAgent works.
            auto& registry = scene::EntityRegistry::getRegistry();
            auto enttEntity = internal::fromHandle(entity);
            if (!registry.valid(enttEntity) ||
                !registry.all_of<components::NavmeshAgentComponent>(enttEntity))
            {
                return;
            }
            addAgent(entity);
            it = entityToAgentIndex.find(entity.id);
            if (it == entityToAgentIndex.end())
            {
                return;  // off-navmesh / crowd full: addCrowdAgent failed
            }
        }
        navmeshProvider->setCrowdAgentTarget(it->second, snapped);
        entityToTarget[entity.id] = snapped;
        entityStuckTimer.erase(entity.id);
    }

    void NavmeshAgentManager::stopAgent(EntityHandle entity)
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it != entityToAgentIndex.end())
        {
            navmeshProvider->stopCrowdAgent(it->second);
            entityToTarget.erase(entity.id);
            entityStuckTimer.erase(entity.id);
            velocityTransitionFrames.erase(entity.id);
        }
    }

    void NavmeshAgentManager::updateAgentConfig(EntityHandle entity, float maxSpeed, float maxAcceleration)
    {
        // Persist onto the component first, so values set before the agent joins
        // the crowd (lazy registration happens on the first setAgentDestination)
        // survive and are read by addAgent when it finally registers.
        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = internal::fromHandle(entity);
        if (registry.valid(enttEntity) && registry.all_of<components::NavmeshAgentComponent>(enttEntity))
        {
            auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
            if (maxSpeed >= 0.0f) agent.maxSpeed = maxSpeed;
            if (maxAcceleration >= 0.0f) agent.maxAcceleration = maxAcceleration;
        }

        auto it = entityToAgentIndex.find(entity.id);
        if (it == entityToAgentIndex.end()) return;

        navmeshProvider->updateCrowdAgentParams(it->second, maxSpeed, maxAcceleration);
    }

    glm::vec3 NavmeshAgentManager::getAgentVelocity(EntityHandle entity) const
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it == entityToAgentIndex.end()) return glm::vec3(0.0f);
        return navmeshProvider->getCrowdAgentVelocity(it->second);
    }

    float NavmeshAgentManager::getAgentSpeed(EntityHandle entity) const
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it == entityToAgentIndex.end()) return 0.0f;
        return navmeshProvider->getCrowdAgentMaxSpeed(it->second);
    }

    void NavmeshAgentManager::updatePositions(float deltaTime)
    {
        if (entityToAgentIndex.empty())
        {
            return;
        }

        navmeshProvider->updateCrowd(deltaTime);

        // Process velocity transitions for recently-resumed agents
        if (!velocityTransitionFrames.empty())
        {
            auto transIt = velocityTransitionFrames.begin();
            while (transIt != velocityTransitionFrames.end())
            {
                transIt->second--;
                if (transIt->second <= 0)
                {
                    auto agentIt = entityToAgentIndex.find(transIt->first);
                    auto targetIt = entityToTarget.find(transIt->first);
                    if (agentIt != entityToAgentIndex.end() && targetIt != entityToTarget.end())
                    {
                        navmeshProvider->setCrowdAgentTarget(agentIt->second, targetIt->second);
                    }
                    transIt = velocityTransitionFrames.erase(transIt);
                }
                else
                {
                    ++transIt;
                }
            }
        }

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

            // Face the travel direction (yaw about Y) when actually moving, so the
            // mesh turns toward its path instead of sliding sideways. rotation is
            // euler degrees; forward is +Z (flip +180 in content if a mesh faces -Z).
            glm::vec3 agentVel = navmeshProvider->getCrowdAgentVelocity(agentIdx);
            if (agentVel.x * agentVel.x + agentVel.z * agentVel.z > 1e-4f)
            {
                transform.rotation.y = glm::degrees(glm::atan(agentVel.x, agentVel.z));
            }

            transform.isDirty = true;

            // Check arrival and blocked state
            auto targetIt = entityToTarget.find(entityId);
            if (targetIt != entityToTarget.end())
            {
                // Read per-agent thresholds from component, fall back to defaults
                float arrivalDist = ARRIVAL_DISTANCE;
                float stuckVelThresh = STUCK_VELOCITY_THRESHOLD;
                float stuckTimeThresh = STUCK_TIME_THRESHOLD;
                if (registry.all_of<components::NavmeshAgentComponent>(enttEntity))
                {
                    const auto& agentComp = registry.get<components::NavmeshAgentComponent>(enttEntity);
                    arrivalDist = agentComp.arrivalDistance;
                    stuckVelThresh = agentComp.stuckVelocityThreshold;
                    stuckTimeThresh = agentComp.stuckTimeThreshold;
                }

                float distToTarget = glm::distance(agentPos, targetIt->second);

                if (distToTarget <= arrivalDist)
                {
                    entityToTarget.erase(targetIt);
                    entityStuckTimer.erase(entityId);

                    events::navmesh::AgentReachedDestinationNotification notif;
                    notif.entity = handle;
                    ::events::EventDispatcher::instance().publish(notif);
                }
                else
                {
                    glm::vec3 velocity = navmeshProvider->getCrowdAgentVelocity(agentIdx);
                    float speed = glm::length(velocity);

                    if (speed < stuckVelThresh)
                    {
                        entityStuckTimer[entityId] += deltaTime;
                        if (entityStuckTimer[entityId] >= stuckTimeThresh)
                        {
                            entityToTarget.erase(targetIt);
                            entityStuckTimer.erase(entityId);

                            events::navmesh::AgentPathBlockedNotification notif;
                            notif.entity = handle;
                            ::events::EventDispatcher::instance().publish(notif);
                        }
                    }
                    else
                    {
                        entityStuckTimer.erase(entityId);
                    }
                }
            }
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
                suspended.velocity = navmeshProvider->getCrowdAgentVelocity(agentIdx);

                auto targetIt = entityToTarget.find(entityId);
                if (targetIt != entityToTarget.end())
                {
                    suspended.target = targetIt->second;
                    suspended.hasTarget = true;
                }
                else
                {
                    suspended.target = glm::vec3(0.0f);
                    suspended.hasTarget = false;
                }

                auto enttEntity = internal::fromHandle(EntityHandle{entityId});
                if (registry.valid(enttEntity) && registry.all_of<components::NavmeshAgentComponent>(enttEntity))
                {
                    auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
                    agent.isSuspended = true;
                    agent.suspendedPosition = agentPos;
                    agent.suspendedTarget = suspended.target;
                    agent.hasSuspendedTarget = suspended.hasTarget;
                }

                navmeshProvider->removeCrowdAgent(agentIdx);
                suspendedAgents.push_back(suspended);
                toRemove.push_back(entityId);
            }
        }

        for (uint64_t id : toRemove)
        {
            entityToAgentIndex.erase(id);
            entityToTarget.erase(id);
            entityStuckTimer.erase(id);
            velocityTransitionFrames.erase(id);
        }

        // Agents standing on still-loaded tiles whose TARGET tile just unloaded
        // would chase an unreachable goal — re-issue the target so Detour
        // re-plans to the nearest reachable point
        for (const auto& [entityId, agentIdx] : entityToAgentIndex)
        {
            auto targetIt = entityToTarget.find(entityId);
            if (targetIt == entityToTarget.end())
                continue;

            if (unloadedSet.count(worldToTileCoord(targetIt->second)))
                navmeshProvider->setCrowdAgentTarget(agentIdx, targetIt->second);
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
                            entityToTarget[it->entityId] = it->target;
                        }

                        // If agent had meaningful velocity, use velocity override for smooth transition
                        float speed = glm::length(it->velocity);
                        if (speed > 0.01f)
                        {
                            navmeshProvider->overrideCrowdAgentVelocity(agentIdx, it->velocity);
                            velocityTransitionFrames[it->entityId] = VELOCITY_TRANSITION_FRAMES;
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
        entityToTarget.clear();
        entityStuckTimer.clear();
        suspendedAgents.clear();
        velocityTransitionFrames.clear();
    }
}
